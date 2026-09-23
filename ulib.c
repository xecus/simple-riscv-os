/**
 * @file ulib.c
 * @brief ユーザープログラム向けの小さなライブラリ
 *
 * システムコールのラッパー、printf、行入力などをまとめる。
 * カーネルとのやり取りはすべて syscall()（usys.c）を経由するため、
 * このファイル自体はアセンブリに依存しない。
 */
#include "user.h"

// 端末から送られてくる編集キー
#define KEY_BACKSPACE 0x08
#define KEY_DELETE    0x7f

// printf の内部ヘルパー（このファイル内だけで使う static 関数）
static void process_format_specifier(char spec, __builtin_va_list *args);
static void print_string(const char *str);
static void print_decimal(int value);
static void print_hexadecimal(unsigned value);


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
    syscall(SYS_EXIT, 0, 0, 0);

    // カーネルは戻ってこない。noreturn を満たすために置いておく
    for (;;);
}


/**
 * @brief コンソールから1行読み取る
 * @param buf 読み取った文字列を格納するバッファ（NUL終端する）
 * @param size buf のサイズ（終端文字を含む）
 * @return 読み取った文字数
 *
 * 改行が来るまで getchar() で1文字ずつ読む。QEMU の -serial mon:stdio は
 * 端末をローモードにするため端末側のエコーが無く、ここでエコーを返す。
 */
int readline(char *buf, int size) {
    int len = 0;

    if (size <= 0) {
        return 0;
    }

    for (;;) {
        int ch = getchar();

        if (ch < 0) {
            continue;   // カーネルが待つので通常は起きない
        }

        // 端末は Enter で CR を送ってくる。LF も念のため受ける
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            break;
        }

        if (ch == KEY_BACKSPACE || ch == KEY_DELETE) {
            if (len > 0) {
                len--;
                // カーソルを戻して空白で消し、もう一度戻す
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }

        // 印字可能文字だけを受け付ける
        if (ch < 0x20 || ch > 0x7e) {
            continue;
        }

        // 満杯になった後の入力は捨てる（エコーもしない）
        if (len < size - 1) {
            buf[len++] = (char) ch;
            putchar((char) ch);
        }
    }

    buf[len] = '\0';
    return len;
}
