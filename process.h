#pragma once
#include "kernel.h"

#define PROCS_MAX 8       // 最大プロセス数

#define PROC_UNUSED   0   // 未使用のプロセス管理構造体
#define PROC_RUNNABLE 1   // 実行可能なプロセス

struct process {
    int pid;             // プロセスID
    int state;           // プロセスの状態: PROC_UNUSED または PROC_RUNNABLE
    vaddr_t sp;          // コンテキストスイッチ時のスタックポインタ
    uint32_t *page_table;
    uint8_t stack[PROCESS_STACK_SIZE]; // カーネルスタック
};

extern char __kernel_base[];
extern struct process *current_proc;
extern struct process *idle_proc;

struct process *create_idle_process(void);
struct process *create_process2(const void *image, size_t image_size);

void switch_context(uint32_t *prev_sp, uint32_t *next_sp);
void yield(void);
