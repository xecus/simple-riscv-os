/**
 * @file harness.c
 * @brief ユニットテスト基盤の実装（起動処理・結果出力・終了処理）
 *
 * テスト用のカーネルイメージとして 0x80200000 から起動する。
 * 出力は printf を使わず sbi_call で直接行う。テスト対象の printf や
 * putchar はモックに差し替えられていることがあるため。
 */
#include "kernel.h"
#include "harness.h"

extern char __bss[], __bss_end[];

static int checks;        // 実行したチェックの数
static int failures;      // 失敗したチェックの数
static int test_failed;   // 実行中のテストで失敗があったか

static void t_putc(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

void t_puts(const char *s) {
    while (*s) {
        t_putc(*s++);
    }
}

void t_putdec(long value) {
    unsigned long magnitude = value < 0 ? -(unsigned long) value : (unsigned long) value;
    char digits[24];
    int n = 0;

    if (value < 0) {
        t_putc('-');
    }

    do {
        digits[n++] = '0' + magnitude % 10;
        magnitude /= 10;
    } while (magnitude);

    while (n > 0) {
        t_putc(digits[--n]);
    }
}

void t_puthex(unsigned long value) {
    t_puts("0x");
    for (int shift = (int) sizeof(value) * 8 - 4; shift >= 0; shift -= 4) {
        t_putc("0123456789abcdef"[(value >> shift) & 0xf]);
    }
}

// 制御文字を見える形にして出力する（期待値との差分を読みやすくするため）
static void t_put_escaped(const char *s) {
    t_putc('"');
    for (; *s; s++) {
        switch (*s) {
            case '\n': t_puts("\\n"); break;
            case '\r': t_puts("\\r"); break;
            case '\b': t_puts("\\b"); break;
            case '"':  t_puts("\\\""); break;
            default:   t_putc(*s); break;
        }
    }
    t_putc('"');
}

static void t_fail(const char *expr, const char *file, int line) {
    failures++;
    test_failed = 1;
    t_puts("  FAIL ");
    t_puts(file);
    t_putc(':');
    t_putdec(line);
    t_puts(": ");
    t_puts(expr);
}

void t_check(int ok, const char *expr, const char *file, int line) {
    checks++;
    if (ok) {
        return;
    }

    t_fail(expr, file, line);
    t_putc('\n');
}

void t_check_eq(unsigned long actual, unsigned long expected,
                const char *expr, const char *file, int line) {
    checks++;
    if (actual == expected) {
        return;
    }

    t_fail(expr, file, line);
    t_puts("\n    actual:   ");
    t_puthex(actual);
    t_puts("\n    expected: ");
    t_puthex(expected);
    t_putc('\n');
}

static int t_streq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

void t_check_str(const char *actual, const char *expected,
                 const char *expr, const char *file, int line) {
    checks++;
    if (t_streq(actual, expected)) {
        return;
    }

    t_fail(expr, file, line);
    t_puts("\n    actual:   ");
    t_put_escaped(actual);
    t_puts("\n    expected: ");
    t_put_escaped(expected);
    t_putc('\n');
}

void t_run(const char *name, void (*fn)(void)) {
    test_failed = 0;
    t_puts("[RUN ] ");
    t_puts(name);
    t_putc('\n');

    fn();

    t_puts(test_failed ? "[FAIL] " : "[ OK ] ");
    t_puts(name);
    t_putc('\n');
}

__attribute__((noreturn))
static void t_shutdown(void) {
    sbi_call(0, 0, 0, 0, 0, 0, 0, SBI_EID_SHUTDOWN);
    for (;;) {}
}

void t_abort(const char *reason) {
    t_puts("\nABORT: ");
    t_puts(reason);
    t_puts("\nTESTS FAILED\n");
    t_shutdown();
}

/**
 * @brief 想定外のトラップを報告して終了する
 *
 * テスト中にページフォルトなどが起きたら、黙って固まらずに原因を出して
 * 失敗として終わらせる。トラップベクタとして使うので4バイト境界に置く。
 */
__attribute__((aligned(4)))
void t_unexpected_trap(void) {
    t_puts("\nUNEXPECTED TRAP: scause=");
    t_puthex(READ_CSR(scause));
    t_puts(" sepc=");
    t_puthex(READ_CSR(sepc));
    t_puts(" stval=");
    t_puthex(READ_CSR(stval));
    t_puts("\nTESTS FAILED\n");
    t_shutdown();
}

void harness_main(void) {
    // memset はテスト対象側にしか無いことがあるので、自前でゼロクリアする
    for (volatile char *p = __bss; p < __bss_end; p++) {
        *p = 0;
    }

    WRITE_CSR(stvec, (unsigned long) t_unexpected_trap);

    t_puts("\n=== unit tests (xlen=");
    t_putdec(__riscv_xlen);
    t_puts(") ===\n");

    run_all_tests();

    t_puts("=== ");
    t_putdec(checks);
    t_puts(" checks, ");
    t_putdec(failures);
    t_puts(" failures ===\n");
    t_puts(failures == 0 && checks > 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");

    t_shutdown();
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "la sp, __stack_top\n"
        "j harness_main\n"
    );
}
