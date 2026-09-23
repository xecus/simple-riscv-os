#include "kernel.h"
#include "memory.h"
#include "exception.h"
#include "process.h"

extern char __bss[], __bss_end[], __stack_top[];
// objcopy -Ibinary が作るシンボル。_binary_shell_bin_size も作られるが、
// あれは「値がサイズ」の絶対シンボルで、アドレスとして参照すると
// RV64 の PC 相対アドレッシング（-mcmodel=medany）では届かない。
// サイズは start と end の差から求める
extern char _binary_shell_bin_start[], _binary_shell_bin_end[];

// Forward declarations for internal functions
static void handle_page_fault(vaddr_t fault_addr, reg_t scause,
                              reg_t fault_pc);
static long syscall_getchar(void);
static void syscall_sleep(uint32_t ms);
static void block_current_process(uint32_t ms);
static void syscall_exit(void);
static void shutdown(void);
static void handle_interrupt(reg_t code);
static void schedule_next_tick(void);
static void init_timer(void);
static void init_process_management(void);
static void create_user_processes(void);

/**
 * @brief コンソールから1文字読む（SBI Console Getchar、legacy 拡張）
 * @return 文字コード。入力が無ければ負の値
 */
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}

/**
 * @brief コンソールへ1文字書く（SBI Console Putchar、legacy 拡張）
 */
void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

/**
 * @brief ユーザーモードへ落ちる入口
 *
 * プロセスが初めてスケジュールされたとき、switch_context() の ret から
 * ここへ来る。起動引数を a0 に載せてから sret するので、ユーザー側の
 * start() は a0 に触らずに main を呼ぶだけでよい。
 *
 * naked を外したのは、current_proc からの読み出しにコード生成が必要な
 * ため。sret から先へは戻らないので、コンパイラが積むプロローグは
 * 使われないまま放置される（カーネルスタックの先頭側に残るだけで、
 * 次のトラップがその領域を上書きしても実害はない）。
 */
void user_entry(void) {
    // ユーザーモードでの a0 の値。RISC-V の呼び出し規約により
    // start() から呼ばれる main の第1引数になる
    register reg_t a0 __asm__("a0") = current_proc->arg;

    // sret の戻り先をユーザーモード（SPP = 0）にし、戻った後は割り込みを
    // 有効にする（SPIE = 1）。値を丸ごと書かずに必要なビットだけ変えるのは、
    // RV64 の sstatus には UXL（ユーザーモードのレジスタ幅）などのフィールドが
    // あり、0 を書くと予約値になるため（書き込みを無視するかは実装次第）
    reg_t sstatus = (READ_CSR(sstatus) & ~(reg_t) (SSTATUS_SPP | SSTATUS_SIE))
                    | SSTATUS_SPIE;

    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (sstatus),
          "r" (a0)
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
    reg_t scause = READ_CSR(scause);    // 例外原因
    reg_t stval = READ_CSR(stval);      // 例外に関連する値（アドレスなど）
    reg_t user_pc = READ_CSR(sepc);     // 例外発生時のPC

    // sepc と sstatus はCSR、つまり全プロセス共通の1組しかない。
    // 処理中に yield() でプロセスが切り替わると別プロセスが書き換えてしまうため、
    // 自分のカーネルスタック上に退避しておき、復帰直前に書き戻す
    reg_t saved_sstatus = READ_CSR(sstatus);

    if (scause & SCAUSE_INTERRUPT) {
        // 割り込み：例外と違い、復帰先のPCは進めずに中断した命令から再開する
        handle_interrupt(scause & ~SCAUSE_INTERRUPT);
        WRITE_CSR(sstatus, saved_sstatus);
        WRITE_CSR(sepc, user_pc);
        return;
    }

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
            PANIC("Unexpected trap: scause=0x%lx, stval=0x%lx, sepc=0x%lx",
                  scause, stval, user_pc);
    }

    // 修正されたPCをCSRに書き戻し（ユーザーモード復帰時に使用）
    WRITE_CSR(sstatus, saved_sstatus);
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
static void handle_page_fault(vaddr_t fault_addr, reg_t scause,
                              reg_t fault_pc) {
    // sstatus.SPP が 1 なら、トラップ元はスーパーバイザモード。
    // カーネル自身のバグなので、ページを割り当てて隠蔽してはいけない
    if (READ_CSR(sstatus) & SSTATUS_SPP) {
        PANIC("page fault in kernel mode: addr=0x%lx scause=%d sepc=0x%lx",
              fault_addr, (int) scause, fault_pc);
    }

    // デマンドページングの対象はユーザー空間に限定する。
    // 範囲外へのアクセスは不正アクセスとして扱う
    if (fault_addr < USER_BASE || fault_addr >= USER_LIMIT) {
        PANIC("invalid memory access by pid=%d: addr=0x%lx scause=%d sepc=0x%lx",
              current_proc->pid, fault_addr, (int) scause, fault_pc);
    }

    // ページ境界にアラインメント（4KB境界に切り下げ）
    vaddr_t vaddr = ALIGN_DOWN(fault_addr, PAGE_SIZE);

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
    reg_t syscall_num = f->a3;  // a3レジスタからシステムコール番号を取得

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

        case SYS_GETPID:
            // プロセスID取得：呼び出し元プロセスのIDを返す
            f->a0 = current_proc->pid;
            break;

        case SYS_SLEEP:
            // 待機：a0レジスタのミリ秒数だけ待つ
            syscall_sleep((uint32_t) f->a0);
            f->a0 = 0; // 成功を示す戻り値
            break;

        case SYS_EXIT:
            // 終了：呼び出し元プロセスを終了させる。ここへは戻ってこない
            syscall_exit();
            break;

        default:
            // 未定義のシステムコール：システムを停止
            PANIC("Unknown system call: %ld", (long) syscall_num);
    }
}

/**
 * @brief コンソールから文字を取得（ブロッキング）
 * @return 文字コード、失敗時はエラー
 * 
 * 入力が無い間は1ティックずつ待機状態に入る。ここで yield() を
 * 直接回してしまうと、カーネルモードのまま（sstatus.SIE = 0 のまま）
 * 実行を続けることになり、タイマ割り込みが一切入らなくなる。
 * 2つ以上のプロセスが同時に入力待ちになると互いを選び合い続け、
 * sleep 中のプロセスを誰も起こせなくなるため、必ず待機状態を経由する。
 */
static long syscall_getchar(void) {
    while (1) {
        long ch = getchar();
        if (ch >= 0) {
            return ch;
        }

        // 1ティック待ってから再確認する。待機中はスケジューラの
        // 候補から外れるので、他に実行可能なプロセスが無ければ
        // アイドルプロセスへ落ち、そこでタイマ割り込みを受けられる。
        //
        // 代償として入力の反応が最大1ティック（10ms）遅れる。
        // ここで yield() に戻すと反応は速くなるが、入力待ちの
        // プロセスが2つ以上あるとタイマ割り込みが永久に入らなくなる
        block_current_process(TICK_INTERVAL_MS);
    }
}

/**
 * @brief 指定ミリ秒だけ待機する
 * @param ms 待機するミリ秒数
 *
 * 起床時刻を記録したうえでプロセスを PROC_SLEEPING にし、CPUを手放す。
 * 待機中はスケジューラの候補から外れるため、CPUをまったく消費しない。
 * タイマ割り込みのたびに wake_expired_processes() が起床時刻を確認し、
 * 時刻が来たプロセスを PROC_RUNNABLE に戻す。
 */
static void syscall_sleep(uint32_t ms) {
    // ユーザー空間から渡された値をそのまま信用しない。負の値が符号なしへ
    // 変換されると数十日の待機になってしまうため、最上位ビットが立っている
    // 値は誤用として扱い、待機しない
    if (ms == 0 || ms > 0x7fffffffu) {
        return;
    }

    block_current_process(ms);
}

/**
 * @brief 現在のプロセスを指定ミリ秒だけ待機状態にしてCPUを手放す
 * @param ms 待機するミリ秒数
 *
 * 待機中のプロセスは yield() の候補から外れるため、CPUを消費しない。
 * タイマ割り込みのたびに wake_expired_processes() が起床時刻を確認する。
 */
static void block_current_process(uint32_t ms) {
    // 64ビットで計算する。32ビットのまま掛けると約429秒で溢れ、
    // 起床時刻が過去になって次のティックで即座に起きてしまう
    current_proc->wake_time = read_time() + (uint64_t) ms * TICKS_PER_MS;
    current_proc->state = PROC_SLEEPING;
    yield();
}

/**
 * @brief 呼び出し元プロセスを終了させる
 *
 * 状態を PROC_EXITED にしてCPUを手放す。yield() は PROC_RUNNABLE の
 * プロセスしか選ばないため、以降このプロセスがスケジュールされることはない。
 *
 * リソースは回収しない。alloc_pages() が解放手段を持たないバンプアロケータで
 * あることと、このOSがプロセスを動的生成しない（起動時に作るだけ）ことによる。
 * カーネルスタックも放置されるが、二度と使われないので安全。
 */
static void syscall_exit(void) {
    int pid = current_proc->pid;

    printf("[kernel] pid %d exited\n", pid);

    current_proc->state = PROC_EXITED;
    yield();

    PANIC("exited process %d was scheduled again", pid);
}

/**
 * @brief SBI 経由で電源を切る
 *
 * QEMU は --no-reboot 付きで起動しているため、そのままプロセスが終了する。
 */
static void shutdown(void) {
    sbi_call(0, 0, 0, 0, 0, 0, 0, SBI_EID_SHUTDOWN);

    PANIC("sbi shutdown did not take effect");
}

/**
 * @brief 割り込みを処理する
 * @param code 割り込みの種類（scause の最上位ビットを除いた値）
 */
static void handle_interrupt(reg_t code) {
    if (code != SCAUSE_TIMER_INTERRUPT) {
        PANIC("Unexpected interrupt: code=%d", (int) code);
    }

    // 先に次のティックを予約する。予約し直さないとタイマ割り込みが
    // 保留されたままになり、同じ割り込みが延々と再発生してしまう
    schedule_next_tick();

    // 起床時刻を過ぎた待機中プロセスを実行可能に戻す
    wake_expired_processes(read_time());

    // 実行中のプロセスからCPUを取り上げ、次のプロセスへ切り替える。
    // プロセスの同意を必要としないこの切り替えがプリエンプション
    yield();
}

/**
 * @brief 次のタイマ割り込みを予約する
 *
 * SBI の Timer 拡張に「この時刻になったら割り込んでくれ」と依頼する。
 * 依頼と同時に、保留中のタイマ割り込みはクリアされる。
 */
static void schedule_next_tick(void) {
    uint64_t next = read_time() + TICK_INTERVAL_MS * TICKS_PER_MS;

    struct sbiret ret = sbi_call((long) next, 0, 0, 0, 0, 0,
                                 SBI_FID_SET_TIMER, SBI_EID_TIME);

    // 予約に失敗するとタイマ割り込みが二度と発生しない。sleep は
    // ポーリングを廃止しているため、誰もプロセスを起こせないまま
    // wfi で静かに停止してしまう。原因の分かる形で即座に落とす
    if (ret.error != 0) {
        PANIC("sbi_set_timer failed: error=%d", (int) ret.error);
    }
}

/**
 * @brief タイマ割り込みを有効にする
 *
 * sie の STIE を立てると、ユーザーモード実行中はタイマ割り込みが
 * 有効になる。RISC-V の仕様では、U-mode で実行している間は
 * sstatus.SIE の値が無視され、S-mode の割り込みは常に有効になる。
 *
 * 逆にカーネル実行中は sstatus.SIE = 0 なので割り込みが入らない。
 * これによりトラップハンドラの多重実行（＝kernel_entry がカーネル
 * スタックを上書きする事故）を防いでいる。
 * 唯一の例外はアイドルループで、そこだけは明示的に SIE を立てる。
 */
static void init_timer(void) {
    schedule_next_tick();

    // ファームウェアが残した設定を引き継がないよう、代入で上書きする。
    // handle_interrupt() はタイマ以外の割り込みで PANIC するため、
    // 有効にする割り込みをここで確定させておく
    WRITE_CSR(sie, SIE_STIE);
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
    // 何よりも先に割り込みを止める。stvec を設定する前にトラップが起きると
    // 飛び先がファームウェアの残した値になり、さらに sscratch も未設定（0）
    // なので kernel_entry がトラップフレームを不正なアドレスへ書き込む。
    // ファームウェアが sstatus.SIE や sie を 0 で渡す保証は無いため、
    // 前提に頼らずここで確定させる
    // マスクはレジスタ幅で作る。32ビットで作ると RV64 では上位32ビット
    // （UXL や SD）まで 0 で消してしまう
    WRITE_CSR(sstatus, READ_CSR(sstatus) & ~(reg_t) SSTATUS_SIE);
    WRITE_CSR(sie, 0);

    // BSS領域をゼロで初期化（C言語の仕様により必要）
    memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

    printf("RISC-V OS Starting... (platform: %s)\n", PLATFORM_NAME);

    // トラップベクタ設定：例外・割り込み時にkernel_entry関数を呼び出し
    WRITE_CSR(stvec, (uintptr_t) kernel_entry);

    // プロセス管理システムの初期化
    init_process_management();

    // タイマ割り込みを有効化（プリエンプションの土台）
    init_timer();

    // ユーザープロセスを作成して実行開始
    create_user_processes();

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
 * @brief ユーザープロセスを作成・開始
 * 
 * ユーザープログラム（shell.bin）をロードしてプロセスとして起動し、
 * その後はアイドルループに入ります。アイドルループは起動時の
 * コンテキストがそのまま使われ、PID 0 のアイドルプロセスとして扱われます。
 */
static void create_user_processes(void) {
    // 同じイメージから2つのプロセスを作る。create_process2() が
    // プロセスごとに物理ページとページテーブルを用意するため、
    // 両者のメモリ空間は完全に独立している。
    // 役割は起動引数で伝える。PID の採番に依存させないため
    size_t shell_size = _binary_shell_bin_end - _binary_shell_bin_start;
    create_process2(_binary_shell_bin_start, shell_size, PROC_ARG_PRINTER);
    create_process2(_binary_shell_bin_start, shell_size, PROC_ARG_CONSOLE);

    yield(); // ユーザープロセスに切り替え

    // アイドルループ：実行可能なプロセスが無いときにここへ戻ってくる。
    // wfi で停止している間もタイマ割り込みを受け取れるように、
    // スーパーバイザモードの割り込みを許可しておく。
    // これが無いと、全プロセスが sleep した時点で誰も起こせなくなる。
    //
    // ここでの割り込みはスーパーバイザモードからの発生になるが、
    // アイドルはブート時のスタックで動いているのに対し sscratch は
    // 未使用の procs[0].stack を指しているため、kernel_entry が積む
    // トラップフレームはブートスタックと衝突しない
    WRITE_CSR(sstatus, READ_CSR(sstatus) | SSTATUS_SIE);

    while (1) {
        // 生きているユーザープロセスがいなくなったら電源を切る。
        // タイマが1ティック（10ms）ごとにここを起こすので、
        // 最大1ティック遅れで検出される
        if (!has_live_process()) {
            printf("[kernel] no live process remains, shutting down\n");
            shutdown();
        }

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
