#pragma once
#include "common.h"

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2

// Core system functions
__attribute__((noreturn)) void exit(void);
int getchar(void);
void putchar(char ch);
void printf(const char *fmt, ...);

// Internal printf helper functions (static declarations)
static void process_format_specifier(char spec, __builtin_va_list *args);
static void print_string(const char *str);
static void print_decimal(int value);
static void print_hexadecimal(unsigned value);
