/**
 * @file test_user.c
 * @brief ユーザー空間ライブラリ（ulib.c）と疑似コンソール（user.c）のテスト
 *
 * static 関数（str_eq、run_console）も確かめたいので、ソースをそのまま
 * このファイルへ取り込む。カーネルとの境界である syscall() だけをモックに
 * 差し替え、入力は台本（scripted_input）から与え、出力は captured に溜める。
 */
#include "harness.h"
#include "ulib.c"
#include "user.c"

// ---------------------------------------------------------------------------
// モック: syscall()
// ---------------------------------------------------------------------------

#define MOCK_PID 3

static char captured[2048];
static int captured_len;

static const char *scripted_input;

// putchar 以外のシステムコールの記録
static struct {
    int sysno;
    int arg0;
} calls[16];
static int call_count;

int syscall(int sysno, int arg0, int arg1, int arg2) {
    (void) arg1;
    (void) arg2;

    if (sysno == SYS_PUTCHAR) {
        if (captured_len < (int) sizeof(captured) - 1) {
            captured[captured_len++] = (char) arg0;
            captured[captured_len] = '\0';
        }
        return 0;
    }

    if (call_count < (int) (sizeof(calls) / sizeof(calls[0]))) {
        calls[call_count].sysno = sysno;
        calls[call_count].arg0 = arg0;
        call_count++;
    }

    switch (sysno) {
        case SYS_GETCHAR:
            // 台本が尽きたら、テストの書き方が誤っている（readline が戻れない）
            if (*scripted_input == '\0') {
                t_abort("mock getchar: scripted input exhausted");
            }
            return (unsigned char) *scripted_input++;

        case SYS_GETPID:
            return MOCK_PID;

        default:
            return 0;
    }
}

static void mock_reset(const char *input) {
    captured_len = 0;
    captured[0] = '\0';
    scripted_input = input;
    call_count = 0;
}

// getchar 以外の記録の数を数える（readline は getchar を大量に呼ぶため）
static int count_calls(int sysno) {
    int n = 0;
    for (int i = 0; i < call_count; i++) {
        n += calls[i].sysno == sysno;
    }
    return n;
}

// ---------------------------------------------------------------------------
// printf
// ---------------------------------------------------------------------------

static void test_printf_decimal(void) {
    mock_reset("");
    printf("%d|%d|%d|%d|%d", 0, 7, -7, 2147483647, -2147483647 - 1);
    CHECK_STR(captured, "0|7|-7|2147483647|-2147483648");
}

static void test_printf_hex_string_percent(void) {
    mock_reset("");
    printf("%x|%x|%s|%s|%%", 0u, 0xdeadbeefu, "abc", (const char *) 0);
    CHECK_STR(captured, "00000000|deadbeef|abc|(null)|%");
}

static void test_printf_edge_cases(void) {
    void (*print)(const char *, ...) = printf;

    // 未対応の変換指定子は % ごとそのまま出す（カーネルの printf とは異なる）
    mock_reset("");
    print("a%qb");
    CHECK_STR(captured, "a%qb");

    mock_reset("");
    print("abc%");
    CHECK_STR(captured, "abc%");
}

// ---------------------------------------------------------------------------
// システムコールのラッパー
// ---------------------------------------------------------------------------

static void test_sleep_arguments(void) {
    // 0 以下は何もしない（システムコールを発行しない）
    mock_reset("");
    sleep_ms(0);
    sleep_ms(-5);
    sleep(0);
    sleep(-1);
    CHECK_EQ(call_count, 0);

    mock_reset("");
    sleep_ms(250);
    sleep(3);
    sleep(3000000);   // int を溢れないよう 2000000 秒に丸める
    CHECK_EQ(call_count, 3);
    CHECK_EQ(calls[0].sysno, SYS_SLEEP);
    CHECK_EQ(calls[0].arg0, 250);
    CHECK_EQ(calls[1].arg0, 3000);
    CHECK_EQ(calls[2].arg0, 2000000000);
}

static void test_getpid_and_getchar(void) {
    mock_reset("Z");
    CHECK_EQ(getpid(), MOCK_PID);
    CHECK_EQ(getchar(), 'Z');
    CHECK_EQ(count_calls(SYS_GETPID), 1);
    CHECK_EQ(count_calls(SYS_GETCHAR), 1);
}

// ---------------------------------------------------------------------------
// readline
// ---------------------------------------------------------------------------

static void test_readline_basic(void) {
    char buf[16];

    mock_reset("abc\r");
    CHECK_EQ(readline(buf, sizeof(buf)), 3);
    CHECK_STR(buf, "abc");
    CHECK_STR(captured, "abc\n");   // エコーし、CR は改行として出す

    // LF でも行が終わる
    mock_reset("xy\n");
    CHECK_EQ(readline(buf, sizeof(buf)), 2);
    CHECK_STR(buf, "xy");

    // 空行
    mock_reset("\r");
    CHECK_EQ(readline(buf, sizeof(buf)), 0);
    CHECK_STR(buf, "");
    CHECK_STR(captured, "\n");
}

static void test_readline_backspace(void) {
    char buf[16];

    // DEL（0x7f）と BS（0x08）のどちらでも1文字消える
    mock_reset("ab\x7f" "c\bd\r");
    CHECK_EQ(readline(buf, sizeof(buf)), 2);
    CHECK_STR(buf, "ad");
    CHECK_STR(captured, "ab\b \bc\b \bd\n");

    // 空の状態でのバックスペースは何もしない（画面も消さない）
    mock_reset("\x7f\x7fz\r");
    CHECK_EQ(readline(buf, sizeof(buf)), 1);
    CHECK_STR(buf, "z");
    CHECK_STR(captured, "z\n");
}

static void test_readline_filters_control_characters(void) {
    char buf[16];

    // 印字できない文字は捨てる。矢印キー（ESC [ A）は ESC だけが捨てられる
    mock_reset("a\x01\t\x1b[A\r");
    CHECK_EQ(readline(buf, sizeof(buf)), 3);
    CHECK_STR(buf, "a[A");
    CHECK_STR(captured, "a[A\n");
}

static void test_readline_overflow(void) {
    char buf[4];

    // 満杯になった後の入力は捨て、エコーもしない
    mock_reset("abcdef\r");
    CHECK_EQ(readline(buf, sizeof(buf)), 3);
    CHECK_STR(buf, "abc");
    CHECK_STR(captured, "abc\n");

    // サイズ1なら終端文字しか入らない
    mock_reset("ab\r");
    CHECK_EQ(readline(buf, 1), 0);
    CHECK_STR(buf, "");

    // サイズ0以下なら入力を読まずに戻る
    mock_reset("q\r");
    CHECK_EQ(readline(buf, 0), 0);
    CHECK_EQ(count_calls(SYS_GETCHAR), 0);
}

// ---------------------------------------------------------------------------
// 疑似コンソール（user.c）
// ---------------------------------------------------------------------------

static void test_str_eq(void) {
    CHECK(str_eq("help", "help"));
    CHECK(str_eq("", ""));
    CHECK(!str_eq("help", "hel"));
    CHECK(!str_eq("hel", "help"));
    CHECK(!str_eq("help", "Help"));
}

static void test_console_session(void) {
    mock_reset("help\r" "hello\r" "pid\r" "foo\r" "\r" "exit\r");
    run_console(MOCK_PID);

    CHECK_STR(captured,
              "\nconsole (pid 3) ready. type 'help' for commands.\n"
              "> help\n"
              "  help   show this help\n"
              "  hello  print a greeting\n"
              "  pid    show this process id\n"
              "  exit   terminate the console\n"
              "> hello\n"
              "Hello from console (pid 3)\n"
              "> pid\n"
              "3\n"
              "> foo\n"
              "unknown command: foo\n"
              "> \n"
              "> exit\n"
              "bye\n");

    // exit で戻った後の入力は読んでいない
    CHECK_STR(scripted_input, "");
}

static void test_console_long_line_is_truncated(void) {
    // 64文字を超える行は 63 文字で切れ、残りは捨てられる
    mock_reset("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r"
               "exit\r");
    run_console(MOCK_PID);

    CHECK_STR(captured,
              "\nconsole (pid 3) ready. type 'help' for commands.\n"
              "> aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
              "unknown command: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
              "> exit\n"
              "bye\n");
}

static void test_main_dispatches_console_role(void) {
    // コンソール役の main は getpid で自分の PID を得て、exit で戻る
    mock_reset("exit\r");
    main(PROC_ARG_CONSOLE);

    CHECK_EQ(count_calls(SYS_GETPID), 1);
    CHECK_STR(captured,
              "\nconsole (pid 3) ready. type 'help' for commands.\n"
              "> exit\n"
              "bye\n");
}

// ---------------------------------------------------------------------------

void run_all_tests(void) {
    RUN(test_printf_decimal);
    RUN(test_printf_hex_string_percent);
    RUN(test_printf_edge_cases);
    RUN(test_sleep_arguments);
    RUN(test_getpid_and_getchar);
    RUN(test_readline_basic);
    RUN(test_readline_backspace);
    RUN(test_readline_filters_control_characters);
    RUN(test_readline_overflow);
    RUN(test_str_eq);
    RUN(test_console_session);
    RUN(test_console_long_line_is_truncated);
    RUN(test_main_dispatches_console_role);
}
