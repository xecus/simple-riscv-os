#pragma once

// common.h は include しない。shell.elf には common.c をリンクしないため、
// memcpy/strcpy/strcmp の宣言だけ見えている状態になってしまう。
// ユーザー空間で必要な宣言はこのファイルに閉じておく。

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define SYS_SLEEP   3
#define SYS_GETPID  4
#define SYS_EXIT    5

// カーネルから main の第1引数として渡される起動引数。
// kernel.h にも同じ値を定義してあるので、変更するときは両方を直すこと
#define PROC_ARG_PRINTER 0
#define PROC_ARG_CONSOLE 1

// システムコールを発行する（usys.c）。a0-a2 に引数、a3 に番号を載せて ecall する
int syscall(int sysno, int arg0, int arg1, int arg2);

// Core system functions
__attribute__((noreturn)) void exit(void);
int getchar(void);
void putchar(char ch);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// 待機。負の値やゼロを渡した場合は何もせずに戻る
void sleep_ms(int ms);
void sleep(int seconds);

// 自プロセスのIDを取得する
int getpid(void);

// コンソールから1行読み取る。読み取った文字数を返す
int readline(char *buf, int size);
