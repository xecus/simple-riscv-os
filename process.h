#pragma once
#include "kernel.h"

#define PROCS_MAX 8       // 最大プロセス数

#define PROC_UNUSED   0   // 未使用のプロセス管理構造体
#define PROC_RUNNABLE 1   // 実行可能なプロセス
#define PROC_SLEEPING 2   // 起床時刻まで待機中。スケジューラの候補から外れる
#define PROC_EXITED   3   // 終了済み。スケジューラの候補から永久に外れる

struct process {
    int pid;             // プロセスID
    int state;           // プロセスの状態: PROC_UNUSED / RUNNABLE / SLEEPING / EXITED
    vaddr_t sp;          // コンテキストスイッチ時のスタックポインタ
    uint32_t *page_table;
    uint64_t wake_time;  // PROC_SLEEPING のときの起床時刻（time CSR の値）

    // カーネルスタック。RISC-V の呼び出し規約はスタックポインタが16バイト
    // 境界にあることを要求する。スタック末尾をそのまま sp / sscratch に
    // 使うため、ここを16バイト境界に揃えて構造体サイズも16の倍数にする
    // （揃えないと wake_time の分だけサイズが 8216 になり、配列の
    //   奇数番目の要素で末尾が8バイト境界にずれる）
    uint8_t stack[PROCESS_STACK_SIZE] __attribute__((aligned(16)));
};

extern char __kernel_base[];
extern struct process *current_proc;
extern struct process *idle_proc;

struct process *create_idle_process(void);
struct process *create_process2(const void *image, size_t image_size);

void switch_context(uint32_t *prev_sp, uint32_t *next_sp);
void yield(void);
void wake_expired_processes(uint64_t now);
int has_live_process(void);
