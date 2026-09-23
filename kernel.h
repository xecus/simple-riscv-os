/**
 * @file kernel.h  
 * @brief Core kernel definitions and structures for RISC-V OS
 * 
 * このファイルは、RISC-V 用のシンプルなOSカーネルの中核となる定数、
 * 構造体、関数宣言を定義します。RV32 と RV64 のどちらでもビルドできるよう、
 * レジスタ幅に依存する部分は reg_t / uintptr_t と __riscv_xlen で書き分けます。
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
typedef unsigned long long uint64_t;   // 64ビット符号なし整数

// RISC-V の ILP32 / LP64 のどちらでも long はポインタ・汎用レジスタと同じ幅
// （RV32 で32ビット、RV64 で64ビット）になる。アドレスやレジスタの値を
// uint32_t で持つと RV64 で上位ビットが落ちるため、必ずこれらを使う
typedef unsigned long uintptr_t;       // ポインタと同じ幅の整数
typedef unsigned long reg_t;           // 汎用レジスタ・CSR と同じ幅の整数
typedef uintptr_t paddr_t;             // 物理アドレス型
typedef uintptr_t vaddr_t;             // 仮想アドレス型

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
#define SSTATUS_SIE     (1 << 1)       // Supervisor Interrupt Enable
                                       // スーパーバイザモードでの割り込み許可

// 割り込み許可レジスタ（sie）のビット定義
#define SIE_STIE        (1 << 5)       // Supervisor Timer Interrupt Enable

// 例外原因コード（RISC-V仕様で定義されている値）
#define SCAUSE_ECALL    8              // ユーザーモードからのシステムコール

// scause の最上位ビット（RV32 ならビット31、RV64 ならビット63）が
// 立っていれば例外ではなく割り込み。残りのビットが割り込みの種類を表す
#define SCAUSE_INTERRUPT        (1UL << (__riscv_xlen - 1))
#define SCAUSE_TIMER_INTERRUPT  5      // スーパーバイザタイマ割り込み

// システムコール番号（このOSで独自に定義）
#define SYS_PUTCHAR 1                  // 文字出力システムコール
#define SYS_GETCHAR 2                  // 文字入力システムコール
#define SYS_SLEEP   3                  // 指定ミリ秒だけ待機するシステムコール
#define SYS_GETPID  4                  // 自プロセスのIDを取得するシステムコール
#define SYS_EXIT    5                  // 呼び出し元プロセスを終了するシステムコール

// ユーザープロセスへ渡す起動引数。プロセス生成時に a0 レジスタへ載せる。
// user.h にも同じ値を定義してあるので、変更するときは両方を直すこと
#define PROC_ARG_PRINTER 0             // 一定間隔で出力し続けるプロセス
#define PROC_ARG_CONSOLE 1             // 行入力を受け付けるプロセス

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

// タイムスライスの長さ。この間隔でタイマ割り込みが発生し、
// 実行中のプロセスから強制的にCPUを取り上げる
#define TICK_INTERVAL_MS    10

// SBI Timer 拡張（経過時間の通知に使う）。
// RV32 では64ビットの時刻を2つの引数（下位・上位）に分けて渡す
#define SBI_EID_TIME        0x54494D45  // "TIME"
#define SBI_FID_SET_TIMER   0

// SBI Shutdown（legacy 拡張）。呼ぶと電源が切れ、QEMU は --no-reboot に
// よってプロセスごと終了する
#define SBI_EID_SHUTDOWN    8

void user_entry(void);

// OpenSBI呼び出しの戻り値構造体
struct sbiret {
    long error;    // エラーコード
    long value;    // 戻り値
};

// トラップフレーム：例外発生時にCPUレジスタを保存する構造体
// RISC-V レジスタセット（exception.cの保存順序と一致）。
// 各要素はレジスタ幅（RV32 で4バイト、RV64 で8バイト）
struct trap_frame {
    reg_t ra, gp, tp, t0, t1, t2, t3, t4, t5, t6, a0, a1, a2, a3, a4, a5, a6, a7,
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

// アセンブリの中でレジスタ幅のロード・ストアを書くための命令名と幅
#if __riscv_xlen == 32
#define REG_L    "lw"
#define REG_S    "sw"
#define REG_SIZE "4"
#else
#define REG_L    "ld"
#define REG_S    "sd"
#define REG_SIZE "8"
#endif

// 値が指定されたアライメント境界に整列しているかチェック
static inline int is_aligned(uintptr_t value, uintptr_t alignment) {
    return (value & (alignment - 1)) == 0;
}

/**
 * @brief 64ビットのタイマカウンタを読む
 *
 * RV64 では time CSR 1つで64ビット全体が読める。
 *
 * RV32 では time CSR が下位32ビット、timeh が上位32ビットに分かれている。
 * 下位を読んだ直後に桁上がりが起きると値が壊れるため、
 * 上位を読み直して一致するまでやり直す。
 */
static inline uint64_t read_time(void) {
#if __riscv_xlen == 64
    return READ_CSR(time);
#else
    uint32_t hi, lo, hi_again;

    do {
        hi       = (uint32_t) READ_CSR(timeh);
        lo       = (uint32_t) READ_CSR(time);
        hi_again = (uint32_t) READ_CSR(timeh);
    } while (hi != hi_again);

    return ((uint64_t) hi << 32) | lo;
#endif
}

// パニック：回復不能なエラーが発生した時にシステムを停止
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}  /* 無限ループでシステムを停止 */                              \
    } while (0)
