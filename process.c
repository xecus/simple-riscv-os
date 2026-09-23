#include "kernel.h"
#include "process.h"
#include "common.h"
#include "memory.h"

extern char __free_ram_end[];

struct process procs[PROCS_MAX];
struct process *current_proc; // 現在実行中のプロセス
struct process *idle_proc;

// 全プロセス共通のカーネル領域マッピングの雛形。
// カーネル領域のマッピングはどのプロセスでも同一なので、一度だけ作って
// 各プロセスには1段目テーブル（1ページ）をコピーするだけで済ませる。
static uint32_t *kernel_page_table;

static void idle_entry(void);

__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // 実行中プロセスのスタックへレジスタを保存
        "addi sp, sp, -13 * 4\n"
        "sw ra,  0  * 4(sp)\n"
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // スタックポインタの切り替え
        "sw sp, (a0)\n"
        "lw sp, (a1)\n"

        // 次のプロセスのスタックからレジスタを復元
        "lw ra,  0  * 4(sp)\n"
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"
        "ret\n"
    );
}

/**
 * @brief アイドルプロセスの本体（現状この関数には到達しない）
 *
 * ブート時のコンテキストがそのままアイドルプロセスになるため、
 * alloc_process() が積んだ ra は最初の switch_context で procs[0].sp ごと
 * 上書きされ、ここへ制御が来ることはない。実際のアイドルループは
 * create_user_processes() の末尾にある。
 *
 * 到達したら設計の前提が崩れているので、黙って回らずに落とす。
 * この経路を生かす場合は、割り込みの許可（sstatus.SIE）だけでは足りず、
 * sscratch を別のスタックへ向ける必要がある。この関数は sscratch が指す
 * スタックそのものの上で動くため、そのままではトラップフレームが
 * 自分のフレームを上書きしてしまう。
 */
static void idle_entry(void) {
    PANIC("idle_entry reached: the boot context should be the idle process");
}

/**
 * @brief プロセス用の1段目ページテーブルを作る
 *
 * カーネル領域のストレートマッピングは全プロセスで共通なので、
 * 初回だけ雛形を構築し、以降は1ページ分のコピーで済ませる。
 * （毎回マップし直すと1プロセスあたり約16,400回の map_page が走る）
 */
static uint32_t *create_page_table(void) {
    if (!kernel_page_table) {
        kernel_page_table = (uint32_t *) alloc_pages(1);
        for (paddr_t paddr = (paddr_t) __kernel_base;
             paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE) {
            map_page(kernel_page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
        }
    }

    uint32_t *page_table = (uint32_t *) alloc_pages(1);
    memcpy(page_table, kernel_page_table, PAGE_SIZE);
    return page_table;
}

/**
 * @brief 空いているプロセス管理構造体を確保して初期化する
 * @param entry switch_context() で最初に復帰する先のアドレス
 * @return 実行可能状態になったプロセス構造体
 */
static struct process *alloc_process(uint32_t entry) {
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");

    // switch_context() で復帰できるように、RISC-V呼び出し先保存レジスタを積む
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = entry;                  // ra

    proc->pid = i + 1;
    proc->sp = (uint32_t) sp;
    proc->page_table = create_page_table();
    proc->wake_time = 0;
    proc->arg = 0;

    // state は最後に設定する。これより前に PROC_RUNNABLE にしてしまうと、
    // ページテーブル未設定のプロセスがスケジューラから見えてしまう
    proc->state = PROC_RUNNABLE;
    return proc;
}

/**
 * @brief アイドルプロセスを作成
 *
 * ユーザープログラムを持たないカーネル内プロセス。ユーザー空間の
 * マッピングを行わないため、user_entry ではなく idle_entry から始まる。
 */
struct process *create_idle_process(void) {
    printf("[create_idle_process]\n");
    struct process *proc = alloc_process((uint32_t) idle_entry);
    printf("page_table=0x%x\n", (uint32_t) proc->page_table);
    return proc;
}

/**
 * @brief ユーザープログラムからプロセスを作成
 * @param image ユーザープログラムのバイナリデータ
 * @param image_size バイナリサイズ
 * @param arg ユーザープログラムへ渡す起動引数（main の第1引数になる）
 * @return 作成されたプロセス構造体
 *
 * プロセス作成の手順：
 * 1. 空きプロセススロットを確保しカーネルスタックを初期化
 * 2. ページテーブル（仮想メモリマップ）作成
 * 3. ユーザープログラムをメモリにロードしてマッピング
 */
struct process *create_process2(const void *image, size_t image_size,
                                uint32_t arg) {

    printf("[create_process2]\n");

    // 空きスロット確保 + カーネルスタック初期化 + ページテーブル作成。
    // ユーザーモードのエントリポイント user_entry から実行を始める
    struct process *proc = alloc_process((uint32_t) user_entry);

    // 起動引数を控えておく。user_entry() がユーザーモードへ落ちる直前に
    // a0 へ載せ、start() がそのまま main の第1引数として渡す
    proc->arg = arg;

    printf("page_table=0x%x\n", (uint32_t) proc->page_table);

    // ユーザーのページをマッピングする
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // コピーするデータがページサイズより小さい場合を考慮
        // https://github.com/nuta/operating-system-in-1000-lines/pull/27
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // 確保したページにデータをコピー
        memcpy((void *) page, image + off, copy_size);

        // ページテーブルにマッピング
        map_page(proc->page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }

    return proc;
}

/**
 * @brief 起床時刻を過ぎた待機中プロセスを実行可能に戻す
 * @param now 現在のタイマカウンタ値
 *
 * タイマ割り込みのたびに呼ばれる。sleep 中のプロセスはスケジューラの
 * 候補から外れているため、ここで PROC_RUNNABLE に戻して初めて
 * 再び実行されるようになる。
 */
void wake_expired_processes(uint64_t now) {
    for (int i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_SLEEPING && now >= procs[i].wake_time) {
            procs[i].state = PROC_RUNNABLE;
        }
    }
}

/**
 * @brief 生きているユーザープロセスが残っているか調べる
 * @return 1つでも残っていれば 1、無ければ 0
 *
 * アイドルプロセス（pid 0）と未使用スロットは数えない。終了済みの
 * プロセスは PROC_EXITED のまま残るので、ここでも除外される。
 */
int has_live_process(void) {
    for (int i = 0; i < PROCS_MAX; i++) {
        if (procs[i].pid <= 0) {
            continue;
        }

        if (procs[i].state == PROC_RUNNABLE || procs[i].state == PROC_SLEEPING) {
            return 1;
        }
    }

    return 0;
}

void yield(void) {
    // 実行可能なプロセスを探す
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // 現在実行中のプロセス以外に、実行可能なプロセスがない。戻って処理を続行する
    if (next == current_proc)
        return;

    // 次に切り替えるスレッドのスタックトップアドレスを、sscratch に保存している
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"

        "csrw sscratch, %[sscratch]\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
      [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // コンテキストスイッチ
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}
