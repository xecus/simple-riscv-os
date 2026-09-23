#include "user.h"

extern char __stack_top[];

// コンソールの設定
#define CONSOLE_BUF_SIZE     64   // 1行あたりの最大文字数（終端文字を含む）
#define PRINTER_INTERVAL_SEC 3    // printer プロセスの出力間隔

// 端末から送られてくる編集キー
#define KEY_BACKSPACE 0x08
#define KEY_DELETE    0x7f

// printf の内部ヘルパー（このファイル内だけで使う static 関数）
static void process_format_specifier(char spec, __builtin_va_list *args);
static void print_string(const char *str);
static void print_decimal(int value);
static void print_hexadecimal(unsigned value);
static int str_eq(const char *a, const char *b);
static void run_console(int pid);
static void run_printer(void);

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
    syscall(SYS_EXIT, 0, 0, 0);

    // カーネルは戻ってこない。noreturn を満たすために置いておく
    for (;;);
}

/**
 * @brief 2つの文字列が一致するか調べる
 *
 * user.c には common.c をリンクしないため strcmp が使えない。
 */
static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
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

/**
 * @brief 疑似コンソール本体
 * @param pid 自プロセスのID（プロンプトの表示に使う）
 *
 * exit が入力されたら戻る。呼び出し元の main が return すると
 * start() が exit() を呼び、プロセスが終了する。
 */
static void run_console(int pid) {
    char line[CONSOLE_BUF_SIZE];

    printf("\nconsole (pid %d) ready. type 'help' for commands.\n", pid);

    for (;;) {
        printf("> ");
        readline(line, CONSOLE_BUF_SIZE);

        if (line[0] == '\0') {
            continue;   // 空行はプロンプトを出し直すだけ
        }

        if (str_eq(line, "help")) {
            printf("  help   show this help\n");
            printf("  hello  print a greeting\n");
            printf("  pid    show this process id\n");
            printf("  exit   terminate the console\n");
        } else if (str_eq(line, "hello")) {
            printf("Hello from console (pid %d)\n", pid);
        } else if (str_eq(line, "pid")) {
            printf("%d\n", pid);
        } else if (str_eq(line, "exit")) {
            printf("bye\n");
            return;
        } else {
            printf("unknown command: %s\n", line);
        }
    }
}

/**
 * @brief 一定間隔で出力し続けるプロセス
 *
 * プリエンプションとスケジューリングが効いていることを目視で確認するための
 * プロセス。コンソールが入力待ちでも独立して動き続ける。
 */
static void run_printer(void) {
    int tick = 0;

    for (;;) {
        printf("[printer] tick %d\n", ++tick);
        sleep(PRINTER_INTERVAL_SEC);
    }
}

/**
 * @brief ユーザープログラムの入口
 * @param arg カーネルが渡した起動引数（PROC_ARG_PRINTER / PROC_ARG_CONSOLE）
 *
 * 同じイメージから2つのプロセスが起動するため、役割を引数で受け取って
 * 分岐する。PID の採番に依存させないための作りにしている。
 */
void main(int arg) {
    if (arg == PROC_ARG_CONSOLE) {
        run_console(getpid());
        return;   // main から戻ると start() が exit() を呼ぶ
    }

    run_printer();
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    // a0 にはカーネル（user_entry）が載せた起動引数が入っている。
    // main の第1引数としてそのまま渡したいので、この関数では a0 に触れない。
    // "r"(__stack_top) を使うと値を載せるレジスタに a0 が選ばれうるため、
    // la 命令で sp へ直接読み込む（la は宛先レジスタしか使わない）
    __asm__ __volatile__(
        "la sp, __stack_top\n"
        "call main\n"
        "call exit\n");
}
