#pragma once
#include "kernel.h"

#define SATP_SV32 (1u << 31)
#define PAGE_V    (1 << 0)   // 有効化ビット
#define PAGE_R    (1 << 1)   // 読み込み可能
#define PAGE_W    (1 << 2)   // 書き込み可能
#define PAGE_X    (1 << 3)   // 実行可能
#define PAGE_U    (1 << 4)   // ユーザーモードでアクセス可能
#define PAGE_A    (1 << 6)   // Accessed: アクセス済み
#define PAGE_D    (1 << 7)   // Dirty: 書き込み済み

paddr_t alloc_pages(uint32_t n);
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags);

/**
 * @brief 指定した仮想アドレスのTLBエントリを無効化する
 *
 * ページテーブルへの書き込みは通常のストア命令に過ぎず、CPUのTLBや
 * ページテーブルウォーカには自動的には反映されない。PTEを更新したら
 * 必ずこの命令でバリアを張る必要がある（無効→有効の更新も含む）。
 */
static inline void flush_tlb_page(uint32_t vaddr) {
    __asm__ __volatile__("sfence.vma %0, zero" : : "r"(vaddr) : "memory");
}
