#include "user.h"

extern char __stack_top[];

// printf の内部ヘルパー（このファイル内だけで使う static 関数）
static void process_format_specifier(char spec, __builtin_va_list *args);
static void print_string(const char *str);
static void print_decimal(int value);
static void print_hexadecimal(unsigned value);

/**
 * @brief システムコール呼び出しインターface
 * @param sysno システムコール番号
 * @param arg0-arg2 システムコール引数
 * @return システムコールの戻り値
 * 
 * RISC-V ecall命令を使用してユーザーモードからカーネルモードに
 * 切り替え、カーネルの機能を呼び出します。
 * 
 * レジスタ使用規約（RISC-V ABI）：
 * - a0-a2: 引数レジスタ  
 * - a3: システムコール番号
 * - a0: 戻り値レジスタ
 */
int syscall(int sysno, int arg0, int arg1, int arg2) {
    // RISC-V ABI に従ってレジスタに引数を配置
    register int a0 __asm__("a0") = arg0;      // 第1引数
    register int a1 __asm__("a1") = arg1;      // 第2引数  
    register int a2 __asm__("a2") = arg2;      // 第3引数
    register int a3 __asm__("a3") = sysno;     // システムコール番号

    // ecall命令でスーパーバイザモード（カーネル）に移行
    __asm__ __volatile__("ecall"
                         : "+r"(a0)            // a0は入出力（戻り値として更新）
                         : "r"(a1), "r"(a2), "r"(a3)  // 入力レジスタ
                         : "memory");          // メモリが変更される可能性

    return a0;  // カーネルからの戻り値
}

int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
}

int getpid(void) {
    return syscall(SYS_GETPID, 0, 0, 0);
}

void putchar(char ch) {
    syscall(SYS_PUTCHAR, ch, 0, 0);
}

/**
 * @brief 指定ミリ秒だけ待機する
 * @param ms 待機するミリ秒数（0以下なら何もしない）
 */
void sleep_ms(int ms) {
    if (ms <= 0) {
        return;
    }

    syscall(SYS_SLEEP, ms, 0, 0);
}

/**
 * @brief 指定秒数だけ待機する
 * @param seconds 待機する秒数（0以下なら何もしない）
 *
 * seconds * 1000 が int を溢れない範囲に制限する。
 */
void sleep(int seconds) {
    if (seconds <= 0) {
        return;
    }

    if (seconds > 2000000) {
        seconds = 2000000;
    }

    sleep_ms(seconds * 1000);
}

/**
 * @brief フォーマット付き文字列出力（簡易printf実装）
 * @param fmt フォーマット文字列（%s, %d, %x, %%をサポート）
 * 
 * この実装は標準ライブラリのprintfの簡易版です。
 * ユーザー空間で動作し、システムコール経由でカーネルの
 * putchar機能を使用して文字を出力します。
 * 
 * サポートするフォーマット指定子：
 * - %s: 文字列
 * - %d: 10進整数
 * - %x: 16進整数（8桁固定）
 * - %%: %文字そのもの
 */
void printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;

            if (*fmt == '\0') {
                putchar('%');
                break;
            }

            // フォーマット指定子を処理
            process_format_specifier(*fmt, &args);
        } else {
            // 通常文字をそのまま出力
            putchar(*fmt);
        }
        fmt++;
    }

    __builtin_va_end(args);
}

/**
 * @brief フォーマット指定子を1つ処理
 * @param spec フォーマット指定子文字 (s, d, x, %)
 * @param args 可変引数リストへのポインタ
 * 
 * printfのフォーマット指定子ごとに適切な変換・出力を行います。
 */
static void process_format_specifier(char spec, __builtin_va_list *args) {
    switch (spec) {
        case '%':
            putchar('%');  // %% -> % 文字をそのまま出力
            break;

        case 's':
            print_string(__builtin_va_arg(*args, const char *));  // 文字列出力
            break;

        case 'd':
            print_decimal(__builtin_va_arg(*args, int));  // 10進数出力
            break;

        case 'x':
            print_hexadecimal(__builtin_va_arg(*args, unsigned));  // 16進数出力
            break;

        default:
            // 未対応のフォーマット指定子：%と指定子をそのまま出力
            putchar('%');
            putchar(spec);
            break;
    }
}

/**
 * @brief 文字列を出力（NULL ポインタ処理付き）
 * @param str 出力する文字列
 * 
 * NULL ポインタが渡された場合は "(null)" を出力します。
 */
static void print_string(const char *str) {
    if (!str) {
        str = "(null)";
    }

    while (*str) {
        putchar(*str++);
    }
}

/**
 * @brief Print a decimal integer
 */
static void print_decimal(int value) {
    if (value == 0) {
        putchar('0');
        return;
    }

    unsigned magnitude = (value < 0) ? -value : value;

    if (value < 0) {
        putchar('-');
    }

    // Find the highest power of 10
    unsigned divisor = 1;
    while (magnitude / divisor >= 10) {
        divisor *= 10;
    }

    // Print digits from highest to lowest
    while (divisor > 0) {
        putchar('0' + (magnitude / divisor));
        magnitude %= divisor;
        divisor /= 10;
    }
}

/**
 * @brief Print an unsigned integer in hexadecimal (8 digits)
 */
static void print_hexadecimal(unsigned value) {
    const char *hex_digits = "0123456789abcdef";

    for (int i = 28; i >= 0; i -= 4) {
        unsigned nibble = (value >> i) & 0xf;
        putchar(hex_digits[nibble]);
    }
}

__attribute__((noreturn)) void exit(void) {
    for (;;);
}

void main(void) {
    int pid = getpid();

    // 2つのプロセスが同じイメージを実行するため、開始タイミングをずらして
    // 出力が重なりにくいようにする。printf は1文字ごとのシステムコールで
    // 途中でプリエンプションされうるため、これは重なる確率を下げるだけで
    // あり、排他制御ではない
    sleep_ms((pid % 2) * 500);

    for (;;) {
        printf("[pid %d] Hello World\n", pid);
        sleep(1);
    }
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "call main\n"
        "call exit\n" ::[stack_top] "r"(__stack_top));
}
