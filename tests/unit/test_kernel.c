/**
 * @file test_kernel.c
 * @brief カーネル側のユニットテスト
 *
 * common.c / memory.c / process.c / exception.c を実物のままリンクし、
 * ハードウェアに依存する残りの部分（putchar、user_entry、handle_trap）を
 * このファイルのモックに差し替えて動かす。
 *
 * 期待値はできるだけ実装ではなく仕様から書く。たとえばページテーブルは
 * RISC-V 特権仕様どおりに自前で辿り（spec_walk）、さらに satp を実際に
 * 有効にして CPU 自身に変換させる。RV32（Sv32）と RV64（Sv39）で
 * 同じテストが通れば、移行前後で挙動が変わっていないと言える。
 */
#include "kernel.h"
#include "common.h"
#include "memory.h"
#include "process.h"
#include "exception.h"
#include "harness.h"

extern char __kernel_base[], __free_ram[], __free_ram_end[];
extern struct process procs[PROCS_MAX];

// RISC-V 特権仕様のページテーブル形式。実装側の定数は使わない
#if __riscv_xlen == 32
#define SPEC_LEVELS    2     // Sv32: 2段
#define SPEC_VPN_BITS  10    // 1段あたり 1024 エントリ
#define SPEC_SATP_MODE (1UL << 31)   // satp.MODE = 1 (Sv32)
#else
#define SPEC_LEVELS    3     // Sv39: 3段
#define SPEC_VPN_BITS  9     // 1段あたり 512 エントリ
#define SPEC_SATP_MODE (8UL << 60)   // satp.MODE = 8 (Sv39)
#endif

#define SPEC_V 0x01
#define SPEC_R 0x02
#define SPEC_W 0x04
#define SPEC_X 0x08
#define SPEC_U 0x10
#define SPEC_A 0x40
#define SPEC_D 0x80

// ユーザーページとカーネルページに期待するフラグ（A/D は実装が事前に立てる）
#define FLAGS_USER   (SPEC_U | SPEC_R | SPEC_W | SPEC_X | SPEC_A | SPEC_D | SPEC_V)
#define FLAGS_KERNEL (SPEC_R | SPEC_W | SPEC_X | SPEC_A | SPEC_D | SPEC_V)

// ---------------------------------------------------------------------------
// モック
// ---------------------------------------------------------------------------

// printf の出力先（common.c の printf はこの putchar を呼ぶ）
static char captured[1024];
static int captured_len;

void putchar(char ch) {
    if (captured_len < (int) sizeof(captured) - 1) {
        captured[captured_len++] = ch;
        captured[captured_len] = '\0';
    }
}

static void capture_reset(void) {
    captured_len = 0;
    captured[0] = '\0';
}

// プロセスが初めてスケジュールされたときの入口（本物は kernel.c でユーザーモードへ落ちる）。
// テストでは、どのプロセスがどの順で選ばれたかを記録して終了させる
static int entered_pids[PROCS_MAX];
static int entered_count;

void user_entry(void) {
    entered_pids[entered_count++] = current_proc->pid;
    current_proc->state = PROC_EXITED;
    yield();

    t_abort("exited process was scheduled again");
}

// kernel_entry（exception.c）が呼ぶトラップハンドラ。受け取ったフレームを控え、
// 復帰時に書き戻されることを確かめるため一部のレジスタを書き換える
static struct trap_frame seen_frame;
static int trap_count;

#define TRAP_NEW_A0 0x600dcafeUL
#define TRAP_NEW_T6 0x0ddba11UL

void handle_trap(struct trap_frame *f) {
    seen_frame = *f;
    trap_count++;

    f->a0 = TRAP_NEW_A0;
    f->t6 = TRAP_NEW_T6;

    // ebreak の次の命令へ進める（圧縮命令 c.ebreak なら2バイト）
    unsigned long pc = READ_CSR(sepc);
    unsigned short insn = *(volatile unsigned short *) pc;
    WRITE_CSR(sepc, pc + ((insn & 3) == 3 ? 4 : 2));
}

// ---------------------------------------------------------------------------
// ヘルパー
// ---------------------------------------------------------------------------

/**
 * @brief 仕様どおりにページテーブルを辿る
 * @return 1: 4KB のリーフが見つかった / 0: 未マップ / -1: 形式が不正
 */
static int spec_walk(unsigned long root, unsigned long va,
                     unsigned long *pa, unsigned long *flags) {
    unsigned long table = root;

    for (int level = SPEC_LEVELS - 1; level >= 0; level--) {
        unsigned long index =
            (va >> (12 + level * SPEC_VPN_BITS)) & ((1UL << SPEC_VPN_BITS) - 1);
        unsigned long pte = ((volatile unsigned long *) table)[index];

        if ((pte & SPEC_V) == 0) {
            return 0;
        }

        unsigned long next = (pte >> 10) << 12;
        if (pte & (SPEC_R | SPEC_W | SPEC_X)) {
            // このOSはスーパーページを使わないので、リーフは最下段にしか無いはず
            if (level != 0) {
                return -1;
            }
            *pa = next | (va & 0xfff);
            *flags = pte & 0x3ff;
            return 1;
        }

        // 中間エントリは V 以外のフラグを持たない（A/D/U も立てない）
        if ((pte & 0x3ff) != SPEC_V) {
            return -1;
        }
        table = next;
    }

    return -1;
}

// 次に alloc_pages() が返すアドレス（0ページの確保は位置を進めない）
static unsigned long next_free_page(void) {
    return (unsigned long) alloc_pages(0);
}

static void reset_processes(void) {
    for (int i = 0; i < PROCS_MAX; i++) {
        procs[i].pid = 0;
        procs[i].state = PROC_UNUSED;
    }

    idle_proc = create_idle_process();
    idle_proc->pid = 0;
    current_proc = idle_proc;
    entered_count = 0;
}

// ---------------------------------------------------------------------------
// common.c: printf
// ---------------------------------------------------------------------------

static void test_printf_decimal(void) {
    capture_reset();
    printf("%d|%d|%d", 0, 42, -42);
    CHECK_STR(captured, "0|42|-42");

    capture_reset();
    printf("%d|%d", 2147483647, -2147483647 - 1);
    CHECK_STR(captured, "2147483647|-2147483648");
}

static void test_printf_hex(void) {
    capture_reset();
    printf("%x|%x|%x", 0u, 0xdeadbeefu, 0x1234u);
    CHECK_STR(captured, "00000000|deadbeef|00001234");
}

static void test_printf_string_and_percent(void) {
    capture_reset();
    printf("[%s][%s] 100%%", "abc", (const char *) NULL);
    CHECK_STR(captured, "[abc][(null)] 100%");
}

static void test_printf_long(void) {
    // %lx は long の幅で桁数が決まる（RV32 で8桁、RV64 で16桁）
    capture_reset();
    printf("%ld|%ld|%lx", 0L, -123456789L, 0x80200000UL);
    CHECK_STR(captured, sizeof(long) == 8 ? "0|-123456789|0000000080200000"
                                          : "0|-123456789|80200000");

    // long の最小値と、上位ビットまで使う値
    capture_reset();
#if __riscv_xlen == 64
    printf("%ld|%lx", -9223372036854775807L - 1, 0xfedcba9876543210UL);
    CHECK_STR(captured, "-9223372036854775808|fedcba9876543210");
#else
    printf("%ld|%lx", -2147483647L - 1, 0xfedcba98UL);
    CHECK_STR(captured, "-2147483648|fedcba98");
#endif
}

static void test_printf_edge_cases(void) {
    // 関数ポインタ経由で呼び、コンパイル時の書式チェックを避ける
    void (*print)(const char *, ...) = printf;

    // %l の後が途切れている場合は、そこまでをそのまま出す
    capture_reset();
    print("abc%l");
    CHECK_STR(captured, "abc%l");

    // 書式文字列が % で終わる場合は % をそのまま出す
    capture_reset();
    print("abc%");
    CHECK_STR(captured, "abc%");

    // 未対応の変換指定子は何も出さずに読み飛ばす（現在の仕様）
    capture_reset();
    print("a%qb");
    CHECK_STR(captured, "ab");
}

// ---------------------------------------------------------------------------
// common.c: メモリ・文字列操作
// ---------------------------------------------------------------------------

static void test_memory_and_string_functions(void) {
    char buf[16];

    CHECK(memset(buf, 'x', sizeof(buf)) == buf);
    CHECK_EQ(buf[0], 'x');
    CHECK_EQ(buf[15], 'x');

    const char src[] = "hello";
    CHECK(memcpy(buf, src, sizeof(src)) == buf);
    CHECK_STR(buf, "hello");
    CHECK_EQ(buf[6], 'x');   // コピーした範囲の外は触らない

    CHECK(strcpy(buf, "abc") == buf);
    CHECK_STR(buf, "abc");

    CHECK(strcmp("abc", "abc") == 0);
    CHECK(strcmp("abc", "abd") < 0);
    CHECK(strcmp("abd", "abc") > 0);
    CHECK(strcmp("ab", "abc") < 0);
    CHECK(strcmp("", "") == 0);
}

// ---------------------------------------------------------------------------
// memory.c: alloc_pages / map_page
// ---------------------------------------------------------------------------

static void test_alloc_pages(void) {
    unsigned long first = (unsigned long) alloc_pages(1);
    CHECK_EQ(first % PAGE_SIZE, 0);
    CHECK(first >= (unsigned long) __free_ram);
    CHECK(first < (unsigned long) __free_ram_end);

    // 次に返るページへ先にゴミを書いておき、確保時にゼロクリアされることを確かめる
    volatile unsigned char *next = (volatile unsigned char *) (first + PAGE_SIZE);
    next[0] = 0xaa;
    next[2 * PAGE_SIZE - 1] = 0xbb;

    unsigned long second = (unsigned long) alloc_pages(2);
    CHECK_EQ(second, first + PAGE_SIZE);
    CHECK_EQ(next[0], 0);
    CHECK_EQ(next[2 * PAGE_SIZE - 1], 0);

    CHECK_EQ(next_free_page(), second + 2 * PAGE_SIZE);
}

static void test_map_page_follows_spec(void) {
    unsigned long root = (unsigned long) alloc_pages(1);
    unsigned long page1 = (unsigned long) alloc_pages(1);
    unsigned long page2 = (unsigned long) alloc_pages(1);
    unsigned long pa, flags;

    map_page((void *) root, USER_BASE, page1, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    map_page((void *) root, USER_BASE + 0x1000, page2, PAGE_R);
    map_page((void *) root, 0x80200000, 0x80200000, PAGE_R | PAGE_W | PAGE_X);

    CHECK_EQ(spec_walk(root, USER_BASE, &pa, &flags), 1);
    CHECK_EQ(pa, page1);
    CHECK_EQ(flags, FLAGS_USER);

    // ページ内オフセットはそのまま物理アドレスに引き継がれる
    CHECK_EQ(spec_walk(root, USER_BASE + 0x1abc, &pa, &flags), 1);
    CHECK_EQ(pa, page2 + 0xabc);
    CHECK_EQ(flags, SPEC_R | SPEC_A | SPEC_D | SPEC_V);

    CHECK_EQ(spec_walk(root, 0x80200000, &pa, &flags), 1);
    CHECK_EQ(pa, 0x80200000);
    CHECK_EQ(flags, FLAGS_KERNEL);

    // マップしていないアドレスは見つからない
    CHECK_EQ(spec_walk(root, USER_BASE + 0x2000, &pa, &flags), 0);
    CHECK_EQ(spec_walk(root, 0x40000000, &pa, &flags), 0);
}

static void test_map_page_allocates_intermediate_tables_once(void) {
    unsigned long root = (unsigned long) alloc_pages(1);
    unsigned long page = (unsigned long) alloc_pages(1);

    // 空のルートに最初の1ページをマップすると、中間テーブルが段数-1枚だけ作られる
    unsigned long before = next_free_page();
    map_page((void *) root, USER_BASE, page, PAGE_R);
    CHECK_EQ(next_free_page() - before, (SPEC_LEVELS - 1) * PAGE_SIZE);

    // 同じ最下段テーブルに収まる隣のページでは、新しいテーブルは作られない
    before = next_free_page();
    map_page((void *) root, USER_BASE + 0x1000, page, PAGE_R);
    CHECK_EQ(next_free_page(), before);
}

/**
 * @brief 作ったページテーブルを実際に satp へ設定し、CPU に変換させる
 *
 * spec_walk は仕様の読み方を自前で書いたものなので、それ自体が間違って
 * いる可能性がある。ここでは本物のハードウェア（QEMU）に変換させ、
 * 仮想アドレス経由の読み書きが狙った物理ページに届くことを確かめる。
 * 変換に失敗するとページフォルトになり、ハーネスが失敗として報告する。
 */
static void test_map_page_translates_on_hardware(void) {
    unsigned long root = (unsigned long) alloc_pages(1);
    volatile unsigned int *page1 = (volatile unsigned int *) alloc_pages(1);
    volatile unsigned int *page2 = (volatile unsigned int *) alloc_pages(1);

    // 実行中のコード・スタック・ページテーブル自身がすべて見えるよう、
    // カーネル領域をストレートマップする（process.c と同じ範囲）
    for (unsigned long pa = (unsigned long) __kernel_base;
         pa < (unsigned long) __free_ram_end; pa += PAGE_SIZE) {
        map_page((void *) root, pa, pa, PAGE_R | PAGE_W | PAGE_X);
    }
    map_page((void *) root, USER_BASE, (unsigned long) page1, PAGE_R | PAGE_W);
    map_page((void *) root, USER_BASE + 0x1000, (unsigned long) page2, PAGE_R | PAGE_W);

    page1[0] = 0x11223344;
    page2[1] = 0;

    unsigned long satp = SPEC_SATP_MODE | (root / PAGE_SIZE);
    __asm__ __volatile__("sfence.vma\ncsrw satp, %0\nsfence.vma" : : "r"(satp) : "memory");

    unsigned int read_via_va = *(volatile unsigned int *) USER_BASE;
    *(volatile unsigned int *) (USER_BASE + 0x1004) = 0xcafef00d;

    __asm__ __volatile__("csrw satp, zero\nsfence.vma" : : : "memory");

    CHECK_EQ(read_via_va, 0x11223344);
    CHECK_EQ(page2[1], 0xcafef00d);
}

// ---------------------------------------------------------------------------
// process.c
// ---------------------------------------------------------------------------

static unsigned char image[PAGE_SIZE + 904];   // 2ページ目は途中で終わる

static void test_create_process2(void) {
    reset_processes();
    capture_reset();

    for (unsigned i = 0; i < sizeof(image); i++) {
        image[i] = (unsigned char) (i * 7 + 3);
    }

    struct process *proc = create_process2(image, sizeof(image), 0x5a);

    CHECK(proc == &procs[1]);
    CHECK_EQ(proc->pid, 2);
    CHECK_EQ(proc->state, PROC_RUNNABLE);
    CHECK_EQ(proc->arg, 0x5a);
    CHECK_EQ(proc->wake_time, 0);

    // switch_context() が最初に復元するレジスタ群：ra = user_entry、s0-s11 = 0
    unsigned long *sp = (unsigned long *) proc->sp;
    CHECK_EQ(sp[0], (unsigned long) user_entry);
    for (int i = 1; i <= 12; i++) {
        CHECK_EQ(sp[i], 0);
    }
    CHECK_EQ((unsigned long) (sp + 13), (unsigned long) &proc->stack[sizeof(proc->stack)]);
    CHECK_EQ((unsigned long) (sp + 13) % 16, 0);

    // イメージはユーザー空間の先頭から、ページ単位でコピーされマップされる
    unsigned long root = (unsigned long) proc->page_table;
    unsigned long pa0, pa1, flags, dummy;

    CHECK_EQ(spec_walk(root, USER_BASE, &pa0, &flags), 1);
    CHECK_EQ(flags, FLAGS_USER);
    CHECK_EQ(spec_walk(root, USER_BASE + PAGE_SIZE, &pa1, &flags), 1);
    CHECK_EQ(flags, FLAGS_USER);
    CHECK_EQ(spec_walk(root, USER_BASE + 2 * PAGE_SIZE, &dummy, &flags), 0);

    const unsigned char *p0 = (const unsigned char *) pa0;
    const unsigned char *p1 = (const unsigned char *) pa1;
    int copied_ok = 1;
    for (unsigned i = 0; i < PAGE_SIZE; i++) {
        copied_ok &= p0[i] == image[i];
    }
    for (unsigned i = 0; i < sizeof(image) - PAGE_SIZE; i++) {
        copied_ok &= p1[i] == image[PAGE_SIZE + i];
    }
    CHECK(copied_ok);

    // イメージの末尾より後ろはゼロ（前のデータが漏れていない）
    CHECK_EQ(p1[sizeof(image) - PAGE_SIZE], 0);
    CHECK_EQ(p1[PAGE_SIZE - 1], 0);

    // カーネル領域はストレートマップされ、ユーザーモードからは見えない
    unsigned long pa;
    CHECK_EQ(spec_walk(root, (unsigned long) __kernel_base, &pa, &flags), 1);
    CHECK_EQ(pa, (unsigned long) __kernel_base);
    CHECK_EQ(flags, FLAGS_KERNEL);
    CHECK_EQ(spec_walk(root, (unsigned long) __free_ram_end - PAGE_SIZE, &pa, &flags), 1);
    CHECK_EQ(pa, (unsigned long) __free_ram_end - PAGE_SIZE);
    CHECK_EQ(spec_walk(root, (unsigned long) __free_ram_end, &pa, &flags), 0);

    // 2つ目のプロセスはユーザーページを共有しない
    struct process *other = create_process2(image, sizeof(image), 0);
    CHECK_EQ(other->pid, 3);
    unsigned long other_pa0;
    CHECK_EQ(spec_walk((unsigned long) other->page_table, USER_BASE, &other_pa0, &flags), 1);
    CHECK(other_pa0 != pa0);
}

static void test_has_live_process(void) {
    reset_processes();
    CHECK_EQ(has_live_process(), 0);   // アイドルプロセスは数えない

    procs[1].pid = 2;
    procs[1].state = PROC_EXITED;
    CHECK_EQ(has_live_process(), 0);

    procs[2].pid = 3;
    procs[2].state = PROC_SLEEPING;
    CHECK_EQ(has_live_process(), 1);

    procs[2].state = PROC_RUNNABLE;
    CHECK_EQ(has_live_process(), 1);

    procs[2].state = PROC_EXITED;
    CHECK_EQ(has_live_process(), 0);
}

static void test_wake_expired_processes(void) {
    reset_processes();

    const uint64_t high = 0x100000000ULL;   // 32ビットを超える時刻

    procs[1].pid = 2;
    procs[1].state = PROC_SLEEPING;
    procs[1].wake_time = high + 5;

    procs[2].pid = 3;
    procs[2].state = PROC_SLEEPING;
    procs[2].wake_time = 100;

    procs[3].pid = 4;
    procs[3].state = PROC_EXITED;
    procs[3].wake_time = 0;

    // 下位32ビットだけ見ると起床時刻を過ぎているが、実際はまだ
    wake_expired_processes(5);
    CHECK_EQ(procs[1].state, PROC_SLEEPING);
    CHECK_EQ(procs[2].state, PROC_SLEEPING);

    wake_expired_processes(100);
    CHECK_EQ(procs[1].state, PROC_SLEEPING);
    CHECK_EQ(procs[2].state, PROC_RUNNABLE);

    wake_expired_processes(high + 4);
    CHECK_EQ(procs[1].state, PROC_SLEEPING);

    wake_expired_processes(high + 5);
    CHECK_EQ(procs[1].state, PROC_RUNNABLE);

    // 終了済みのプロセスは起こさない
    CHECK_EQ(procs[3].state, PROC_EXITED);
}

/**
 * @brief yield() が実行可能なプロセスを順に選び、実際に切り替えることを確かめる
 *
 * 各プロセスは user_entry（モック）から始まり、自分の PID を記録して終了する。
 * 最後にどのプロセスも実行可能でなくなると、アイドル（このテスト）へ戻る。
 * satp の切り替えも本物なので、カーネル領域のマッピングが壊れていれば
 * ここでページフォルトになる。
 */
static void test_yield_round_robin(void) {
    reset_processes();
    capture_reset();

    create_process2(image, sizeof(image), 0);   // pid 2
    create_process2(image, sizeof(image), 0);   // pid 3
    create_process2(image, sizeof(image), 0);   // pid 4
    procs[2].state = PROC_SLEEPING;             // pid 3 は選ばれないはず

    // 呼び出し先保存レジスタに載っている値が、切り替えを挟んでも保たれること
    unsigned long v0 = 0x1111, v1 = 0x2222, v2 = 0x3333, v3 = 0x4444,
                  v4 = 0x5555, v5 = 0x6666, v6 = 0x7777, v7 = 0x8888;
    __asm__ __volatile__("" : "+r"(v0), "+r"(v1), "+r"(v2), "+r"(v3),
                              "+r"(v4), "+r"(v5), "+r"(v6), "+r"(v7));

    yield();

    __asm__ __volatile__("" : "+r"(v0), "+r"(v1), "+r"(v2), "+r"(v3),
                              "+r"(v4), "+r"(v5), "+r"(v6), "+r"(v7));

    CHECK(current_proc == idle_proc);
    CHECK_EQ(entered_count, 2);
    CHECK_EQ(entered_pids[0], 2);
    CHECK_EQ(entered_pids[1], 4);
    CHECK_EQ(procs[2].state, PROC_SLEEPING);

    CHECK_EQ(v0, 0x1111);
    CHECK_EQ(v1, 0x2222);
    CHECK_EQ(v2, 0x3333);
    CHECK_EQ(v3, 0x4444);
    CHECK_EQ(v4, 0x5555);
    CHECK_EQ(v5, 0x6666);
    CHECK_EQ(v6, 0x7777);
    CHECK_EQ(v7, 0x8888);

    // 戻ってきた時点で satp はアイドルプロセスのページテーブルを指している
    CHECK_EQ(READ_CSR(satp), SPEC_SATP_MODE | ((unsigned long) idle_proc->page_table / PAGE_SIZE));
    CHECK_EQ(READ_CSR(sscratch), (unsigned long) &idle_proc->stack[sizeof(idle_proc->stack)]);

    __asm__ __volatile__("csrw satp, zero\nsfence.vma" : : : "memory");

    // 全員が実行可能なら、スロット順（PID 順）に1つずつ選ばれる
    reset_processes();
    capture_reset();
    create_process2(image, sizeof(image), 0);   // pid 2
    create_process2(image, sizeof(image), 0);   // pid 3
    create_process2(image, sizeof(image), 0);   // pid 4

    yield();

    CHECK_EQ(entered_count, 3);
    CHECK_EQ(entered_pids[0], 2);
    CHECK_EQ(entered_pids[1], 3);
    CHECK_EQ(entered_pids[2], 4);

    __asm__ __volatile__("csrw satp, zero\nsfence.vma" : : : "memory");
}

// ---------------------------------------------------------------------------
// exception.c: kernel_entry
// ---------------------------------------------------------------------------

#if __riscv_xlen == 64
#define T_SREG "sd"
#define T_WORD "8"
#else
#define T_SREG "sw"
#define T_WORD "4"
#endif

// asm 内で使う out[] の添字
#define OUT_A0     0    // a0-a7: 0-7
#define OUT_T0     8    // t0-t6: 8-14
#define OUT_S2     15   // s2-s11: 15-24
#define OUT_SP_IN  25
#define OUT_SP_OUT 26

static unsigned long out[32];
static unsigned long trap_stack[1024] __attribute__((aligned(16)));

/**
 * @brief トラップの入口がすべてのレジスタを退避・復元することを確かめる
 *
 * 各レジスタに既知の値を入れて ebreak でトラップさせる。ハンドラ（モック）が
 * 受け取ったトラップフレームの中身が struct trap_frame のフィールドと
 * 一致すること、ハンドラが書き換えた値が復帰後のレジスタに反映されること、
 * それ以外のレジスタが元の値のまま戻ることを確認する。
 */
static void test_trap_entry_saves_and_restores_registers(void) {
    unsigned long stack_top = (unsigned long) &trap_stack[1024];
    trap_count = 0;

    WRITE_CSR(sscratch, stack_top);
    WRITE_CSR(stvec, (unsigned long) kernel_entry);

    register unsigned long *base __asm__("s1") = out;
    __asm__ __volatile__(
        T_SREG " sp, 25*" T_WORD "(s1)\n"
        "li a0, 0xa0\n" "li a1, 0xa1\n" "li a2, 0xa2\n" "li a3, 0xa3\n"
        "li a4, 0xa4\n" "li a5, 0xa5\n" "li a6, 0xa6\n" "li a7, 0xa7\n"
        "li t0, 0x70\n" "li t1, 0x71\n" "li t2, 0x72\n" "li t3, 0x73\n"
        "li t4, 0x74\n" "li t5, 0x75\n" "li t6, 0x76\n"
        "li s2, 0x52\n" "li s3, 0x53\n" "li s4, 0x54\n" "li s5, 0x55\n"
        "li s6, 0x56\n" "li s7, 0x57\n" "li s8, 0x58\n" "li s9, 0x59\n"
        "li s10, 0x5a\n" "li s11, 0x5b\n"
        "ebreak\n"
        T_SREG " a0, 0*" T_WORD "(s1)\n"  T_SREG " a1, 1*" T_WORD "(s1)\n"
        T_SREG " a2, 2*" T_WORD "(s1)\n"  T_SREG " a3, 3*" T_WORD "(s1)\n"
        T_SREG " a4, 4*" T_WORD "(s1)\n"  T_SREG " a5, 5*" T_WORD "(s1)\n"
        T_SREG " a6, 6*" T_WORD "(s1)\n"  T_SREG " a7, 7*" T_WORD "(s1)\n"
        T_SREG " t0, 8*" T_WORD "(s1)\n"  T_SREG " t1, 9*" T_WORD "(s1)\n"
        T_SREG " t2, 10*" T_WORD "(s1)\n" T_SREG " t3, 11*" T_WORD "(s1)\n"
        T_SREG " t4, 12*" T_WORD "(s1)\n" T_SREG " t5, 13*" T_WORD "(s1)\n"
        T_SREG " t6, 14*" T_WORD "(s1)\n"
        T_SREG " s2, 15*" T_WORD "(s1)\n" T_SREG " s3, 16*" T_WORD "(s1)\n"
        T_SREG " s4, 17*" T_WORD "(s1)\n" T_SREG " s5, 18*" T_WORD "(s1)\n"
        T_SREG " s6, 19*" T_WORD "(s1)\n" T_SREG " s7, 20*" T_WORD "(s1)\n"
        T_SREG " s8, 21*" T_WORD "(s1)\n" T_SREG " s9, 22*" T_WORD "(s1)\n"
        T_SREG " s10, 23*" T_WORD "(s1)\n" T_SREG " s11, 24*" T_WORD "(s1)\n"
        T_SREG " sp, 26*" T_WORD "(s1)\n"
        :
        : "r"(base)
        : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
          "t0", "t1", "t2", "t3", "t4", "t5", "t6",
          "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
          "memory");

    WRITE_CSR(stvec, (unsigned long) t_unexpected_trap);

    CHECK_EQ(trap_count, 1);

    // ハンドラが受け取ったフレーム（struct trap_frame のレイアウトと一致しているか）
    CHECK_EQ(seen_frame.a0, 0xa0);
    CHECK_EQ(seen_frame.a1, 0xa1);
    CHECK_EQ(seen_frame.a2, 0xa2);
    CHECK_EQ(seen_frame.a3, 0xa3);
    CHECK_EQ(seen_frame.a4, 0xa4);
    CHECK_EQ(seen_frame.a5, 0xa5);
    CHECK_EQ(seen_frame.a6, 0xa6);
    CHECK_EQ(seen_frame.a7, 0xa7);
    CHECK_EQ(seen_frame.t0, 0x70);
    CHECK_EQ(seen_frame.t1, 0x71);
    CHECK_EQ(seen_frame.t2, 0x72);
    CHECK_EQ(seen_frame.t3, 0x73);
    CHECK_EQ(seen_frame.t4, 0x74);
    CHECK_EQ(seen_frame.t5, 0x75);
    CHECK_EQ(seen_frame.t6, 0x76);
    CHECK_EQ(seen_frame.s1, (unsigned long) out);
    CHECK_EQ(seen_frame.s2, 0x52);
    CHECK_EQ(seen_frame.s3, 0x53);
    CHECK_EQ(seen_frame.s4, 0x54);
    CHECK_EQ(seen_frame.s5, 0x55);
    CHECK_EQ(seen_frame.s6, 0x56);
    CHECK_EQ(seen_frame.s7, 0x57);
    CHECK_EQ(seen_frame.s8, 0x58);
    CHECK_EQ(seen_frame.s9, 0x59);
    CHECK_EQ(seen_frame.s10, 0x5a);
    CHECK_EQ(seen_frame.s11, 0x5b);
    CHECK_EQ(seen_frame.sp, out[OUT_SP_IN]);

    // 復帰後のレジスタ：ハンドラが書き換えたものは新しい値、それ以外は元の値
    CHECK_EQ(out[OUT_A0 + 0], TRAP_NEW_A0);
    for (int i = 1; i < 8; i++) {
        CHECK_EQ(out[OUT_A0 + i], 0xa0 + i);
    }
    for (int i = 0; i < 6; i++) {
        CHECK_EQ(out[OUT_T0 + i], 0x70 + i);
    }
    CHECK_EQ(out[OUT_T0 + 6], TRAP_NEW_T6);
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(out[OUT_S2 + i], 0x52 + i);
    }
    CHECK_EQ(out[OUT_SP_OUT], out[OUT_SP_IN]);

    // 次のトラップに備えて sscratch はスタックの先頭に戻っている
    CHECK_EQ(READ_CSR(sscratch), stack_top);
}

// ---------------------------------------------------------------------------

void run_all_tests(void) {
    RUN(test_printf_decimal);
    RUN(test_printf_hex);
    RUN(test_printf_string_and_percent);
    RUN(test_printf_long);
    RUN(test_printf_edge_cases);
    RUN(test_memory_and_string_functions);
    RUN(test_alloc_pages);
    RUN(test_map_page_follows_spec);
    RUN(test_map_page_allocates_intermediate_tables_once);
    RUN(test_map_page_translates_on_hardware);
    RUN(test_create_process2);
    RUN(test_has_live_process);
    RUN(test_wake_expired_processes);
    RUN(test_yield_round_robin);
    RUN(test_trap_entry_saves_and_restores_registers);
}
