/**
 * @file platform.h
 * @brief Milk-V Duo の CPU とメモリ容量に寄せた QEMU 向けのプラットフォーム定数
 *
 * QEMU の virt マシンを、T-Head C906 の CPU モデル（-cpu thead-c906）と
 * 64MB の DRAM で動かす。Milk-V Duo そのものではなく、実機に近い条件で
 * QEMU のテストを回すためのもの。定義すべきマクロの一覧は
 * platform/qemu-virt/platform.h を参照。
 *
 * milkv-duo との違い:
 *   - TIMER_FREQ_HZ は QEMU の値。QEMU の time CSR は CPU モデルに
 *     関わらず 10MHz で進む
 *   - PTE_ATTR_NORMAL_MEM は 0。QEMU は C906 の MAEE を再現せず、
 *     ビット60〜62 を予約ビットとして扱うため
 */
#pragma once

#define PLATFORM_NAME       "qemu-c906"
#define TIMER_FREQ_HZ       10000000u
#define PTE_ATTR_NORMAL_MEM 0UL
