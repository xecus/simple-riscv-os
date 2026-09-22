#pragma once

// common.h は include しない。shell.elf には common.c をリンクしないため、
// memcpy/strcpy/strcmp の宣言だけ見えている状態になってしまう。
// ユーザー空間で必要な宣言はこのファイルに閉じておく。

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2

// Core system functions
__attribute__((noreturn)) void exit(void);
int getchar(void);
void putchar(char ch);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
