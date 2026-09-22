#include "kernel.h"
#include "memory.h"
#include "exception.h"
#include "process.h"

extern char __bss[], __bss_end[], __stack_top[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];

// Forward declarations for internal functions
static void handle_page_fault(uint32_t fault_addr, uint32_t scause,
                              uint32_t fault_pc);
static long syscall_getchar(void);
static void init_process_management(void);
static void create_user_process(void);

__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE)
    );
}

/**
 * @brief ユーザーモードからのトラップ（例外・システムコール）を処理
 * @param f トラップフレーム（保存されたレジスタ群）
 * 
 * RISC-Vでは、例外やシステムコールが発生すると、CPUが自動的に
 * スーパーバイザモードに切り替わり、この関数が呼び出されます。
 * 
 * 処理の流れ：
 * 1. 例外原因（scause）を読み取り
 * 2. 例外の種類に応じて適切な処理を実行
 * 3. ユーザーモードに復帰
 */
void handle_trap(struct trap_frame *f) {
    // RISC-V CSRから例外情報を取得
    uint32_t scause = READ_CSR(scause);    // 例外原因
    uint32_t stval = READ_CSR(stval);      // 例外に関連する値（アドレスなど）
    uint32_t user_pc = READ_CSR(sepc);     // 例外発生時のPC

    switch (scause) {
        case SCAUSE_ECALL:
            // システムコール：ユーザープログラムがカーネルの機能を呼び出し
            handle_syscall(f);
            user_pc += 4; // ecall命令の次の命令に進む
            break;

        case SCAUSE_INST_PAGE_FAULT:    // 命令フェッチでページがない
        case SCAUSE_LOAD_PAGE_FAULT:    // データ読み取りでページがない  
        case SCAUSE_STORE_PAGE_FAULT:   // データ書き込みでページがない
            // ページフォルト：必要なメモリページを動的に割り当て（デマンドページング）
            handle_page_fault(stval, scause, user_pc);
            break;

        default:
            // 未対応の例外：システムを停止
            PANIC("Unexpected trap: scause=0x%x, stval=0x%x, sepc=0x%x",
                  scause, stval, user_pc);
    }

    // 修正されたPCをCSRに書き戻し（ユーザーモード復帰時に使用）
    WRITE_CSR(sepc, user_pc);
}

/**
 * @brief ページフォルトを処理してメモリページを動的に割り当て
 * @param fault_addr ページフォルトが発生した仮想アドレス
 * @param scause 例外原因（命令フェッチ/読み込み/書き込みの区別）
 * @param fault_pc フォルトを起こした命令のアドレス
 *
 * デマンドページング：プログラムが実際にメモリにアクセスした時に
 * 初めてそのページを割り当てる方式。これにより：
 * - メモリ使用量を最小限に抑える
 * - 大きなプログラムでも起動を高速化
 * - 未使用部分にメモリを浪費しない
 *
 * ただし、無条件に割り当ててはならない。検査を省くと、ヌルポインタ参照も
 * カーネル領域への書き込みも「新しいページを割り当てて再実行」で
 * 黙って成功してしまい、メモリ保護が成立しなくなる。
 */
static void handle_page_fault(uint32_t fault_addr, uint32_t scause,
                              uint32_t fault_pc) {
    // sstatus.SPP が 1 なら、トラップ元はスーパーバイザモード。
    // カーネル自身のバグなので、ページを割り当てて隠蔽してはいけない
    if (READ_CSR(sstatus) & SSTATUS_SPP) {
        PANIC("page fault in kernel mode: addr=0x%x scause=%d sepc=0x%x",
              fault_addr, (int) scause, fault_pc);
    }

    // デマンドページングの対象はユーザー空間に限定する。
    // 範囲外へのアクセスは不正アクセスとして扱う
    if (fault_addr < USER_BASE || fault_addr >= USER_LIMIT) {
        PANIC("invalid memory access by pid=%d: addr=0x%x scause=%d sepc=0x%x",
              current_proc->pid, fault_addr, (int) scause, fault_pc);
    }

    // ページ境界にアラインメント（4KB境界に切り下げ）
    uint32_t vaddr = ALIGN_DOWN(fault_addr, PAGE_SIZE);

    // 新しい物理ページを1つ割り当て
    paddr_t paddr = alloc_pages(1);

    // 仮想アドレスと物理アドレスをマッピング
    // フルアクセス権限（読み取り/書き込み/実行 + ユーザーアクセス可能）
    map_page(current_proc->page_table, vaddr, paddr,
             PAGE_U | PAGE_R | PAGE_W | PAGE_X);

    // PTEを書き換えただけではTLBに反映されないため、明示的に無効化する
    flush_tlb_page(vaddr);
}

/**
 * @brief ユーザー空間からのシステムコールを処理
 * @param f システムコール引数を含むトラップフレーム
 * 
 * RISC-V呼び出し規約：
 * - a0-a2: システムコール引数
 * - a3: システムコール番号
 * - a0: 戻り値（システムコール完了後）
 * 
 * システムコールは、ユーザープログラムがカーネルの特権機能
 * （ファイルアクセス、メモリ管理、I/Oなど）を安全に呼び出す仕組み
 */
void handle_syscall(struct trap_frame *f) {
    uint32_t syscall_num = f->a3;  // a3レジスタからシステムコール番号を取得

    switch (syscall_num) {
        case SYS_PUTCHAR:
            // 文字出力：a0レジスタの文字をコンソールに出力
            putchar((char)f->a0);
            f->a0 = 0; // 成功を示す戻り値
            break;

        case SYS_GETCHAR:
            // 文字入力：コンソールから1文字読み取り、a0レジスタに格納
            f->a0 = syscall_getchar();
            break;

        default:
            // 未定義のシステムコール：システムを停止
            PANIC("Unknown system call: %d", (int) syscall_num);
    }
}

/**
 * @brief コンソールから文字を取得（ブロッキング）
 * @return 文字コード、失敗時はエラー
 * 
 * 入力待ちの間、他のプロセスにCPU時間を譲ることで
 * システム全体の応答性を保ちます。
 */
static long syscall_getchar(void) {
    while (1) {
        long ch = getchar();
        if (ch >= 0) {
            return ch;
        }
        yield(); // 入力待ちの間、他のプロセスを実行
    }
}

/**
 * @brief カーネルメイン関数：OSの初期化と起動
 * 
 * このOSの起動シーケンス：
 * 1. BSS領域（未初期化グローバル変数）をゼロクリア
 * 2. 例外ハンドラを登録
 * 3. プロセス管理システムを初期化
 * 4. ユーザープロセスを作成・実行開始
 * 5. アイドルループに入る
 */
void kernel_main(void) {
    // BSS領域をゼロで初期化（C言語の仕様により必要）
    memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

    printf("RISC-V OS Starting...\n");

    // トラップベクタ設定：例外・割り込み時にkernel_entry関数を呼び出し
    WRITE_CSR(stvec, (uint32_t)kernel_entry);

    // プロセス管理システムの初期化
    init_process_management();

    // ユーザープロセスを作成して実行開始
    create_user_process();

    // ここには到達しない（アイドルプロセスが永続実行）
    PANIC("Kernel main returned unexpectedly");
}

/**
 * @brief プロセス管理システムの初期化
 * 
 * アイドルプロセス：他に実行可能なプロセスがない時に
 * 実行される特別なプロセス（PID=0）
 */
static void init_process_management(void) {
    // アイドルプロセスを作成（他のプロセスが実行可能でない時に動作）
    idle_proc = create_idle_process();
    idle_proc->pid = 0;  // 特別なプロセスID 0を割り当て
    current_proc = idle_proc;
}

/**
 * @brief メインユーザープロセスを作成・開始
 * 
 * ユーザープログラム（shell.bin）をロードしてプロセスとして起動し、
 * その後はアイドルループに入ります。
 */
static void create_user_process(void) {
    create_process2(_binary_shell_bin_start, (size_t)_binary_shell_bin_size);
    yield(); // ユーザープロセスに切り替え

    // アイドルループ：ユーザープロセスが実行可能でない時に実行
    while (1) {
        __asm__ __volatile__("wfi"); // 割り込み待ち（省電力）
    }
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
