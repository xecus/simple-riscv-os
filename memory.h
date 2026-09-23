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
#define PTE_PPN_BITS  44             // PPN の幅。その上（ビット54以上）は属性用

#define PAGE_V    (1 << 0)   // 有効化ビット
#define PAGE_R    (1 << 1)   // 読み込み可能
#define PAGE_W    (1 << 2)   // 書き込み可能
#define PAGE_X    (1 << 3)   // 実行可能
#define PAGE_U    (1 << 4)   // ユーザーモードでアクセス可能
#define PAGE_A    (1 << 6)   // Accessed: アクセス済み
#define PAGE_D    (1 << 7)   // Dirty: 書き込み済み

paddr_t alloc_pages(uint32_t n);
void map_page(pte_t *root, vaddr_t vaddr, paddr_t paddr, pte_t flags);

/**
 * @brief PTE が指す物理アドレスを取り出す
 *
 * PTE のビット54以上はプラットフォーム固有の属性（PTE_ATTR_NORMAL_MEM）
 * に使われることがあるため、単純に右シフトするだけでは PPN に混ざる。
 * PPN の幅でマスクしてから物理アドレスに直す。
 */
static inline paddr_t pte_to_paddr(pte_t pte) {
    return ((pte >> PTE_PPN_SHIFT) & ((1UL << PTE_PPN_BITS) - 1)) * PAGE_SIZE;
}

/**
 * @brief データとして書き込んだ命令を、命令フェッチから見えるようにする
 *
 * RISC-V では、ストアで書いた内容が同じハートの命令フェッチに反映される
 * 保証は fence.i を実行するまで無い。QEMU では問題にならないが、実機
 * （T-Head C906 など）は命令キャッシュとデータキャッシュを自動では
 * 同期しないため、古い内容や不定な命令を実行してしまう。
 * プログラムをメモリへコピーした後、実行する前に必ず呼ぶこと。
 * このOSはシングルハートなので、自ハートに対する fence.i で足りる。
 */
static inline void sync_icache(void) {
    __asm__ __volatile__("fence.i" : : : "memory");
}

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
