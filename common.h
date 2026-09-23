#pragma once

typedef int bool;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long size_t;
typedef unsigned long uintptr_t;   // kernel.h と同じ定義にすること
typedef uintptr_t paddr_t;
typedef uintptr_t vaddr_t;

#define PAGE_SIZE 4096

#define true  1
#define false 0
#define offsetof(type, member)   __builtin_offsetof(type, member)
#define va_list  __builtin_va_list
#define va_start __builtin_va_start
#define va_end   __builtin_va_end
#define va_arg   __builtin_va_arg

// 注意: align_up / is_aligned をここでマクロ定義しないこと。
// kernel.h が同名の static inline 関数を持っており、両方を include した
// ファイル（process.c）でマクロが静かに関数を上書きしてしまうため。
// アライメント判定は kernel.h の is_aligned() を使う。

void *memset(void *buf, char c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);
void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

// printf の出力先。カーネルでは kernel.c が SBI 経由で実装し、
// ユニットテストでは出力を捕捉するモックに差し替える
void putchar(char ch);
void delay(void);
