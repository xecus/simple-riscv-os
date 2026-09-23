#!/bin/bash
# ユニットテストと E2E テストをまとめて実行する（Linux / WSL 用）
#
# 使い方: tests/run_tests.sh
#
# ユニットテストはテスト用の小さなカーネルとしてビルドし、QEMU 上で
# 実機と同じ幅・同じ特権モードで動かす（tests/unit/harness.h を参照）。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-clang}

FAILED=0

run_unit_image() {
    local name=$1
    shift
    local out="$ROOT/build/unit"
    local elf="$out/$name.elf"
    local log="$out/$name.log"

    mkdir -p "$out"
    echo "[BUILD] unit/$name"
    # コンパイルフラグは build.sh と揃えること
    if ! $CC -std=c11 -O2 -g3 -Wall -Wextra --target=riscv64-unknown-elf -mcmodel=medany -fno-stack-protector \
            -ffreestanding -nostdlib -I"$ROOT" -I"$ROOT/tests/unit" \
            -Wl,-T"$ROOT/tests/unit/test.ld" -o "$elf" \
            "$ROOT/tests/unit/harness.c" "$ROOT/sbi.c" "$@"; then
        echo "[FAIL] unit/$name: build failed"
        return 1
    fi

    timeout 60 qemu-system-riscv64 -machine virt -bios default -nographic -serial stdio -monitor none \
        --no-reboot -kernel "$elf" < /dev/null > "$log" 2>&1

    # OpenSBI のバナーを除き、テストの出力だけを表示する
    sed -n '/^=== unit tests/,$p' "$log" | grep -v '^\[RUN \]'

    if [ "$(grep -c '^ALL TESTS PASSED' "$log")" != 1 ]; then
        echo "[FAIL] unit/$name: see $log"
        return 1
    fi
}

run_unit_image test_kernel "$ROOT/tests/unit/test_kernel.c" \
    "$ROOT/common.c" "$ROOT/memory.c" "$ROOT/process.c" "$ROOT/exception.c" \
    || FAILED=1

run_unit_image test_user "$ROOT/tests/unit/test_user.c" \
    || FAILED=1

python3 "$ROOT/tests/e2e/e2e.py" || FAILED=1

if [ $FAILED -ne 0 ]; then
    echo "SOME TESTS FAILED"
    exit 1
fi
echo "ALL TESTS PASSED"
