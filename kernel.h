/**
 * @file kernel.h  
 * @brief Core kernel definitions and structures for RISC-V OS
 * 
 * このファイルは、RISC-V 32ビットアーキテクチャ用のシンプルなOSカーネルの
 * 中核となる定数、構造体、関数宣言を定義します。
 * 
 * 主要な機能：
 * - メモリ管理（仮想メモリ、ページング）
 * - プロセス管理とコンテキストスイッチ
 * - システムコール処理
 * - 例外・割り込み処理
 */
#pragma once

// 基本的な型定義
typedef unsigned int uint32_t;         // 32ビット符号なし整数
typedef unsigned long size_t;          // サイズを表す型
typedef unsigned char uint8_t;         // 8ビット符号なし整数  
typedef uint32_t paddr_t;              // 物理アドレス型
typedef uint32_t vaddr_t;              // 仮想アドレス型

#define NULL ((void *)0)

// メモリレイアウト定数
#define PAGE_SIZE       4096           // ページサイズ（4KB）
#define USER_BASE       0x1000000      // ユーザー空間開始アドレス（16MB）
#define USER_LIMIT      0x1800000      // ユーザー空間終端アドレス（24MB）
                                       // user.ld の ASSERT と同じ値にすること

// RISC-V CSR（制御状態レジスタ）ビット定義
#define SSTATUS_SPIE    (1 << 5)       // Supervisor Previous Interrupt Enable
                                       // スーパーバイザモード割り込み有効フラグ
#define SSTATUS_SPP     (1 << 8)       // Supervisor Previous Privilege
                                       // 0ならトラップ元はユーザーモード

// 例外原因コード（RISC-V仕様で定義されている値）
#define SCAUSE_ECALL    8              // ユーザーモードからのシステムコール

// システムコール番号（このOSで独自に定義）
#define SYS_PUTCHAR 1                  // 文字出力システムコール
#define SYS_GETCHAR 2                  // 文字入力システムコール
#define SYS_SLEEP   3                  // 指定ミリ秒だけ待機するシステムコール

// RISC-V ページフォルト例外コード
#define SCAUSE_INST_PAGE_FAULT  12     // 命令フェッチ時のページフォルト
#define SCAUSE_LOAD_PAGE_FAULT  13     // データ読み込み時のページフォルト
#define SCAUSE_STORE_PAGE_FAULT 15     // データ書き込み時のページフォルト

// メモリアライメント
#define ALIGN_DOWN(value, align) ((value) & ~((align) - 1))

// プロセスごとのカーネルスタックサイズ
#define PROCESS_STACK_SIZE  (8 * 1024)    // 8KB

// タイマ関連定数
// QEMU virt マシンの mtimer は 10MHz で動作する（OpenSBI の起動ログに
// "Platform Timer Device : aclint-mtimer @ 10000000Hz" として出る）。
// time CSR はこの周波数でカウントアップするので、経過時間の測定に使える。
#define TIMER_FREQ_HZ   10000000u         // タイマ周波数（10MHz）
#define TICKS_PER_MS    (TIMER_FREQ_HZ / 1000)  // 1ミリ秒あたりのカウント数

// RV32 の time CSR は32ビット。符号なしの差分で経過時間を測るため、
// 一度に待てるのは約429秒まで。これを超える要求は上限に丸める
#define SLEEP_MAX_MS    (0xffffffffu / TICKS_PER_MS)

void user_entry(void);

// OpenSBI呼び出しの戻り値構造体
struct sbiret {
    long error;    // エラーコード
    long value;    // 戻り値
};

// トラップフレーム：例外発生時にCPUレジスタを保存する構造体
// RISC-V レジスタセット（exception.cの保存順序と一致）
struct trap_frame {
    uint32_t ra, gp, tp, t0, t1, t2, t3, t4, t5, t6, a0, a1, a2, a3, a4, a5, a6, a7,
             s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, sp;
};

// RISC-V CSR（制御状態レジスタ）読み取りマクロ
#define READ_CSR(reg) ({                                                       \
    unsigned long __tmp;                                                       \
    __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                      \
    __tmp;                                                                     \
})

// RISC-V CSR（制御状態レジスタ）書き込みマクロ
#define WRITE_CSR(reg, val) ({                                                 \
    __asm__ __volatile__("csrw " #reg ", %0" : : "r"(val));                    \
})

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid);
void putchar(char ch);
long getchar(void);

// format属性を付けることで、書式指定子と引数の型不一致をコンパイル時に検出する
void printf(const char *format, ...) __attribute__((format(printf, 1, 2)));

void *memset(void *buf, char c, size_t n);
void handle_syscall(struct trap_frame *f);

// 値が指定されたアライメント境界に整列しているかチェック
static inline int is_aligned(uint32_t value, uint32_t alignment) {
    return (value & (alignment - 1)) == 0;
}

// パニック：回復不能なエラーが発生した時にシステムを停止
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}  /* 無限ループでシステムを停止 */                              \
    } while (0)
