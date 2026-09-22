#pragma once
#include "common.h"

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2

// Core system functions
__attribute__((noreturn)) void exit(void);
int getchar(void);
void putchar(char ch);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
