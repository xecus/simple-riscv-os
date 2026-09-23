/**
 * @file platform.h
 * @brief QEMU virt マシン向けのプラットフォーム定数
 *
 * プラットフォームごとに異なる値だけをここに置く。共通コードは
 * kernel.h 経由でこのファイルを読み、#ifdef で分岐しない。
 * どのディレクトリの platform.h が読まれるかは build.sh の PLATFORM で決まる。
 *
 * 定義すべきマクロ（他のプラットフォームを足すときも同じものを揃えること）:
 *   PLATFORM_NAME        起動時に表示する名前
 *   TIMER_FREQ_HZ        time CSR がカウントアップする周波数
 *   PTE_ATTR_NORMAL_MEM  通常メモリのリーフ PTE に追加で立てるビット
 */
#pragma once

#define PLATFORM_NAME       "qemu-virt"

// QEMU virt マシンの mtimer は 10MHz で動作する（OpenSBI の起動ログに
// "Platform Timer Device : aclint-mtimer @ 10000000Hz" として出る）
#define TIMER_FREQ_HZ       10000000u

// 標準の RISC-V では PTE のビット 54 以上は予約（または Svpbmt 等の拡張用）
// なので何も立てない
#define PTE_ATTR_NORMAL_MEM 0UL
