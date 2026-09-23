#include "user.h"

// コンソールの設定
#define CONSOLE_BUF_SIZE     64   // 1行あたりの最大文字数（終端文字を含む）
#define PRINTER_INTERVAL_SEC 3    // printer プロセスの出力間隔

static int str_eq(const char *a, const char *b);
static void run_console(int pid);
static void run_printer(void);

/**
 * @brief 2つの文字列が一致するか調べる
 *
 * ユーザープログラムには common.c をリンクしないため strcmp が使えない。
 */
static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
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
