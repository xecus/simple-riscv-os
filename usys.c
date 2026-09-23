/**
 * @file usys.c
 * @brief ユーザー空間のうち、命令列を直接書く必要がある部分
 *
 * プログラムの入口 start() と、カーネルを呼び出す syscall() だけを置く。
 * それ以外のライブラリ関数（ulib.c）やアプリケーション（user.c）は
 * 純粋な C で書けるため、ここから切り離してある。
 */
#include "user.h"

/**
 * @brief システムコール呼び出しインターface
 * @param sysno システムコール番号
 * @param arg0-arg2 システムコール引数
 * @return システムコールの戻り値
 * 
 * RISC-V ecall命令を使用してユーザーモードからカーネルモードに
 * 切り替え、カーネルの機能を呼び出します。
 * 
 * レジスタ使用規約（RISC-V ABI）：
 * - a0-a2: 引数レジスタ  
 * - a3: システムコール番号
 * - a0: 戻り値レジスタ
 */
int syscall(int sysno, int arg0, int arg1, int arg2) {
    // RISC-V ABI に従ってレジスタに引数を配置
    register int a0 __asm__("a0") = arg0;      // 第1引数
    register int a1 __asm__("a1") = arg1;      // 第2引数  
    register int a2 __asm__("a2") = arg2;      // 第3引数
    register int a3 __asm__("a3") = sysno;     // システムコール番号

    // ecall命令でスーパーバイザモード（カーネル）に移行
    __asm__ __volatile__("ecall"
                         : "+r"(a0)            // a0は入出力（戻り値として更新）
                         : "r"(a1), "r"(a2), "r"(a3)  // 入力レジスタ
                         : "memory");          // メモリが変更される可能性

    return a0;  // カーネルからの戻り値
}

__attribute__((section(".text.start")))
__attribute__((naked))
void start(void) {
    // a0 にはカーネル（user_entry）が載せた起動引数が入っている。
    // main の第1引数としてそのまま渡したいので、この関数では a0 に触れない。
    // "r"(__stack_top) を使うと値を載せるレジスタに a0 が選ばれうるため、
    // la 命令で sp へ直接読み込む（la は宛先レジスタしか使わない）
    __asm__ __volatile__(
        "la sp, __stack_top\n"
        "call main\n"
        "call exit\n");
}
