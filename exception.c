#include "kernel.h"
#include "exception.h"

/**
 * @brief トラップの入口（stvec に登録する）
 *
 * sscratch に置いてあるカーネルスタックへ切り替え、全レジスタを
 * struct trap_frame の並びで積んでから handle_trap() を呼ぶ。
 * レジスタ幅のロード・ストア（REG_L / REG_S）と1要素の大きさ（REG_SIZE）は
 * kernel.h で RV32 / RV64 に応じて決まる。
 */
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {

    __asm__ __volatile__(
        "csrrw sp, sscratch, sp\n"

        // 退避するレジスタは31本だが、確保するのは32要素分。
        // 31要素だと sp が16バイト境界から外れ、RISC-V の呼び出し規約に
        // 反した状態で handle_trap を呼ぶことになる。末尾1要素は詰め物
        "addi sp, sp, -" REG_SIZE " * 32\n"
        REG_S " ra,  " REG_SIZE " * 0(sp)\n"
        REG_S " gp,  " REG_SIZE " * 1(sp)\n"
        REG_S " tp,  " REG_SIZE " * 2(sp)\n"
        REG_S " t0,  " REG_SIZE " * 3(sp)\n"
        REG_S " t1,  " REG_SIZE " * 4(sp)\n"
        REG_S " t2,  " REG_SIZE " * 5(sp)\n"
        REG_S " t3,  " REG_SIZE " * 6(sp)\n"
        REG_S " t4,  " REG_SIZE " * 7(sp)\n"
        REG_S " t5,  " REG_SIZE " * 8(sp)\n"
        REG_S " t6,  " REG_SIZE " * 9(sp)\n"
        REG_S " a0,  " REG_SIZE " * 10(sp)\n"
        REG_S " a1,  " REG_SIZE " * 11(sp)\n"
        REG_S " a2,  " REG_SIZE " * 12(sp)\n"
        REG_S " a3,  " REG_SIZE " * 13(sp)\n"
        REG_S " a4,  " REG_SIZE " * 14(sp)\n"
        REG_S " a5,  " REG_SIZE " * 15(sp)\n"
        REG_S " a6,  " REG_SIZE " * 16(sp)\n"
        REG_S " a7,  " REG_SIZE " * 17(sp)\n"
        REG_S " s0,  " REG_SIZE " * 18(sp)\n"
        REG_S " s1,  " REG_SIZE " * 19(sp)\n"
        REG_S " s2,  " REG_SIZE " * 20(sp)\n"
        REG_S " s3,  " REG_SIZE " * 21(sp)\n"
        REG_S " s4,  " REG_SIZE " * 22(sp)\n"
        REG_S " s5,  " REG_SIZE " * 23(sp)\n"
        REG_S " s6,  " REG_SIZE " * 24(sp)\n"
        REG_S " s7,  " REG_SIZE " * 25(sp)\n"
        REG_S " s8,  " REG_SIZE " * 26(sp)\n"
        REG_S " s9,  " REG_SIZE " * 27(sp)\n"
        REG_S " s10, " REG_SIZE " * 28(sp)\n"
        REG_S " s11, " REG_SIZE " * 29(sp)\n"
        "csrr a0, sscratch\n"
        REG_S " a0,  " REG_SIZE " * 30(sp)\n"

        "addi a0, sp, " REG_SIZE " * 32\n"
        "csrw sscratch, a0\n"

        "mv a0, sp\n"
        "call handle_trap\n"

        REG_L " ra,  " REG_SIZE " * 0(sp)\n"
        REG_L " gp,  " REG_SIZE " * 1(sp)\n"
        REG_L " tp,  " REG_SIZE " * 2(sp)\n"
        REG_L " t0,  " REG_SIZE " * 3(sp)\n"
        REG_L " t1,  " REG_SIZE " * 4(sp)\n"
        REG_L " t2,  " REG_SIZE " * 5(sp)\n"
        REG_L " t3,  " REG_SIZE " * 6(sp)\n"
        REG_L " t4,  " REG_SIZE " * 7(sp)\n"
        REG_L " t5,  " REG_SIZE " * 8(sp)\n"
        REG_L " t6,  " REG_SIZE " * 9(sp)\n"
        REG_L " a0,  " REG_SIZE " * 10(sp)\n"
        REG_L " a1,  " REG_SIZE " * 11(sp)\n"
        REG_L " a2,  " REG_SIZE " * 12(sp)\n"
        REG_L " a3,  " REG_SIZE " * 13(sp)\n"
        REG_L " a4,  " REG_SIZE " * 14(sp)\n"
        REG_L " a5,  " REG_SIZE " * 15(sp)\n"
        REG_L " a6,  " REG_SIZE " * 16(sp)\n"
        REG_L " a7,  " REG_SIZE " * 17(sp)\n"
        REG_L " s0,  " REG_SIZE " * 18(sp)\n"
        REG_L " s1,  " REG_SIZE " * 19(sp)\n"
        REG_L " s2,  " REG_SIZE " * 20(sp)\n"
        REG_L " s3,  " REG_SIZE " * 21(sp)\n"
        REG_L " s4,  " REG_SIZE " * 22(sp)\n"
        REG_L " s5,  " REG_SIZE " * 23(sp)\n"
        REG_L " s6,  " REG_SIZE " * 24(sp)\n"
        REG_L " s7,  " REG_SIZE " * 25(sp)\n"
        REG_L " s8,  " REG_SIZE " * 26(sp)\n"
        REG_L " s9,  " REG_SIZE " * 27(sp)\n"
        REG_L " s10, " REG_SIZE " * 28(sp)\n"
        REG_L " s11, " REG_SIZE " * 29(sp)\n"
        REG_L " sp,  " REG_SIZE " * 30(sp)\n"
        "sret\n"
    );
}
