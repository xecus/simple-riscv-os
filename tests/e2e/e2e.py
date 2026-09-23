#!/usr/bin/env python3
"""
E2E テスト: 実際にビルドした OS を QEMU で起動し、シリアル経由で操作する。

ユニットテストが部品ごとの挙動を確かめるのに対し、こちらはユーザーモード、
システムコール、タイマ割り込み、プロセス切り替えがつながった状態で、
利用者から見える振る舞いが変わっていないことを確かめる。

使い方: python3 tests/e2e/e2e.py
"""
import os
import re
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD = os.path.join(ROOT, "build", "e2e")

PRINTER_INTERVAL = 3.0   # user.c の PRINTER_INTERVAL_SEC


def qemu_command(kernel):
    # tests/run_tests.sh の QEMU 設定と揃えること
    return ["qemu-system-riscv64", "-machine", "virt", "-bios", "default",
            "-nographic", "-serial", "stdio", "-monitor", "none", "--no-reboot",
            "-kernel", kernel]


def build(name, user_main):
    out = os.path.join(BUILD, name)
    env = dict(os.environ, OUT=out, USER_MAIN=user_main)
    result = subprocess.run(["bash", os.path.join(ROOT, "build.sh")], env=env)
    if result.returncode != 0:
        raise Failure("build failed (USER_MAIN=%s)" % user_main)
    return os.path.join(out, "kernel.elf")


class Failure(Exception):
    pass


class Qemu:
    """QEMU を子プロセスとして動かし、シリアル出力を溜めながら照合する"""

    def __init__(self, kernel):
        self.proc = subprocess.Popen(qemu_command(kernel), stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.output = ""
        self.cursor = 0       # ここより前の出力は照合済み
        self.cond = threading.Condition()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        # OpenSBI のコンソールは "\n" を "\r\n" にして出すので "\n" に戻す。
        # 読み取りの区切りで "\r" と "\n" が分かれることがあるため、
        # 末尾の "\r" は次の読み取りまで保留する
        pending = ""
        while True:
            data = self.proc.stdout.read1(4096)
            if not data:
                break
            text = (pending + data.decode("utf-8", "replace")).replace("\r\n", "\n")
            pending = ""
            if text.endswith("\r"):
                text, pending = text[:-1], "\r"
            with self.cond:
                self.output += text
                self.cond.notify_all()
        with self.cond:
            self.output += pending
            self.cond.notify_all()

    def expect(self, pattern, timeout=10.0):
        """未照合の出力から pattern（正規表現）を探し、見つかった位置まで進める"""
        regex = re.compile(pattern)
        deadline = time.monotonic() + timeout
        with self.cond:
            while True:
                m = regex.search(self.output, self.cursor)
                if m:
                    self.cursor = m.end()
                    return m
                remaining = deadline - time.monotonic()
                if remaining <= 0 or (self.proc.poll() is not None and not self.reader.is_alive()):
                    raise Failure("timeout waiting for %r\n--- unmatched output ---\n%s"
                                  % (pattern, self.output[self.cursor:]))
                self.cond.wait(remaining)

    def expect_text(self, text, timeout=10.0):
        return self.expect(re.escape(text), timeout)

    def send(self, text):
        # 1文字ずつ送る（SBI の getchar はティックごとのポーリングなので急がない）
        for ch in text:
            self.proc.stdin.write(ch.encode())
            self.proc.stdin.flush()
            time.sleep(0.005)

    def wait_exit(self, timeout):
        try:
            return self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            raise Failure("QEMU did not exit within %ss" % timeout)

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.reader.join(timeout=2)


def without_printer_lines(text):
    return re.sub(r"\[printer\] tick \d+\n", "", text)


def check_no_panic(qemu):
    with qemu.cond:
        if "PANIC" in qemu.output:
            raise Failure("kernel panicked:\n" + qemu.output)


def scenario_console():
    """通常の user.c: 疑似コンソールの操作と、printer の周期・継続"""
    q = Qemu(build("console", "user.c"))
    try:
        q.expect_text("RISC-V OS Starting...")
        m = q.expect(r"console \(pid (\d+)\) ready\. type 'help' for commands\.\n> ")
        pid = m.group(1)
        if pid != "3":
            raise Failure("console pid is %s, expected 3" % pid)

        # printer の出力と行が混ざらないよう、tick を見た直後にまとめて操作する
        tick = int(q.expect(r"\[printer\] tick (\d+)\n").group(1))

        session = [
            ("help\r", "help\n"
                       "  help   show this help\n"
                       "  hello  print a greeting\n"
                       "  pid    show this process id\n"
                       "  exit   terminate the console\n> "),
            ("hello\r", "hello\nHello from console (pid 3)\n> "),
            ("pid\r", "pid\n3\n> "),
            ("foo\r", "foo\nunknown command: foo\n> "),
            ("\r", "\n> "),
            ("hellp\x7fo\r", "hellp\b \bo\nHello from console (pid 3)\n> "),
            ("a" * 70 + "\r", "a" * 63 + "\nunknown command: " + "a" * 63 + "\n> "),
        ]
        for keys, expected in session:
            start = q.cursor
            q.send(keys)
            # どの応答も次のプロンプト（改行 + "> "）で終わり、途中には現れない
            q.expect_text("\n> ")
            with q.cond:
                got = without_printer_lines(q.output[start:q.cursor])
            if got != expected:
                raise Failure("console output mismatch for %r\n  got:      %r\n  expected: %r"
                              % (keys, got, expected))

        q.send("exit\r")
        q.expect_text("exit\nbye\n")
        q.expect_text("[kernel] pid 3 exited\n")

        # コンソールが終わった後も printer は同じ周期で動き続ける。
        # 操作中に出た tick は照合で読み飛ばしていることがあるので、
        # exit 後に最初に見えた tick を起点に連番と間隔を確かめる
        m = q.expect(r"\[printer\] tick (\d+)\n", timeout=PRINTER_INTERVAL * 3)
        next_tick = int(m.group(1))
        if next_tick <= tick:
            raise Failure("printer tick did not advance: %d -> %d" % (tick, next_tick))
        times = [time.monotonic()]
        next_tick += 1
        while len(times) < 3:
            m = q.expect(r"\[printer\] tick (\d+)\n", timeout=PRINTER_INTERVAL * 3)
            n = int(m.group(1))
            if n != next_tick:
                raise Failure("printer tick jumped: got %d, expected %d" % (n, next_tick))
            next_tick += 1
            times.append(time.monotonic())

        for a, b in zip(times, times[1:]):
            interval = b - a
            if not (PRINTER_INTERVAL - 1.0 <= interval <= PRINTER_INTERVAL + 1.0):
                raise Failure("printer interval %.2fs is out of range" % interval)

        check_no_panic(q)
    finally:
        q.close()


def scenario_exit_and_shutdown():
    """tests/e2e/exit_test.c: 全プロセスの終了、デマンドページング、自動シャットダウン"""
    q = Qemu(build("exit", "tests/e2e/exit_test.c"))
    try:
        q.expect_text("RISC-V OS Starting...")
        # 役割と PID、プロセスごとに独立したメモリ、終了の順序
        q.expect_text("[A] pid 2 arg 0\n")
        q.expect_text("[B] pid 3 arg 1\n")
        q.expect_text("[B] probe 00005678\n")
        q.expect_text("[B] return\n")
        q.expect_text("[kernel] pid 3 exited\n")
        q.expect_text("[A] probe 00001234\n")
        q.expect_text("[A] exit\n")
        q.expect_text("[kernel] pid 2 exited\n")
        q.expect_text("[kernel] no live process remains, shutting down\n")

        code = q.wait_exit(timeout=10)
        if code != 0:
            raise Failure("QEMU exited with code %d" % code)

        check_no_panic(q)
    finally:
        q.close()


SCENARIOS = [scenario_console, scenario_exit_and_shutdown]


def main():
    failed = 0
    for scenario in SCENARIOS:
        name = scenario.__name__
        print("[RUN ] e2e/%s" % name, flush=True)
        started = time.monotonic()
        try:
            scenario()
        except Failure as e:
            failed += 1
            print("[FAIL] e2e/%s: %s" % (name, e), flush=True)
            continue
        except Exception as e:
            # QEMU が落ちた後の書き込み（BrokenPipeError）や起動失敗なども
            # そのシナリオの失敗として数え、残りのシナリオは続けて実行する
            failed += 1
            print("[FAIL] e2e/%s: %s: %s" % (name, type(e).__name__, e), flush=True)
            continue
        print("[ OK ] e2e/%s (%.1fs)" % (name, time.monotonic() - started), flush=True)

    if failed:
        print("E2E TESTS FAILED (%d/%d)" % (failed, len(SCENARIOS)))
        return 1
    print("ALL E2E TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
