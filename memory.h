#pragma once
#include "kernel.h"

// ページテーブルの形式は Sv39（3段）。1つのテーブルは 512 エントリ x 8バイトで
// ちょうど1ページ（4KB）に収まる。PTE の下位10ビットがフラグ、
// その上が物理ページ番号（PPN）
#define SATP_MODE      (8UL << 60)   // satp.MODE = 8: Sv39
#define PT_LEVELS      3             // テーブルの段数
#define PT_INDEX_BITS  9             // 1段あたりの添字のビット数

typedef reg_t pte_t;                 // PTE は64ビット

#define PTE_PPN_SHIFT 10             // PTE 内で PPN が始まるビット位置

#define PAGE_V    (1 << 0)   // 有効化ビット
#define PAGE_R    (1 << 1)   // 読み込み可能
#define PAGE_W    (1 << 2)   // 書き込み可能
#define PAGE_X    (1 << 3)   // 実行可能
#define PAGE_U    (1 << 4)   // ユーザーモードでアクセス可能
#define PAGE_A    (1 << 6)   // Accessed: アクセス済み
#define PAGE_D    (1 << 7)   // Dirty: 書き込み済み

paddr_t alloc_pages(uint32_t n);
void map_page(pte_t *root, vaddr_t vaddr, paddr_t paddr, uint32_t flags);

/**
 * @brief 指定した仮想アドレスのTLBエントリを無効化する
 *
 * ページテーブルへの書き込みは通常のストア命令に過ぎず、CPUのTLBや
 * ページテーブルウォーカには自動的には反映されない。PTEを更新したら
 * 必ずこの命令でバリアを張る必要がある（無効→有効の更新も含む）。
 */
static inline void flush_tlb_page(vaddr_t vaddr) {
    __asm__ __volatile__("sfence.vma %0, zero" : : "r"(vaddr) : "memory");
}
