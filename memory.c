#include "kernel.h"
#include "memory.h"

extern char __free_ram[], __free_ram_end[];

/**
 * @brief 物理ページを n 枚まとめて確保する
 *
 * 単純なバンプアロケータで、確保したページを解放する手段は持たない。
 */
paddr_t alloc_pages(uint32_t n) {

    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}

/**
 * @brief 仮想アドレスから、指定した段のテーブルの添字を取り出す
 * @param level 段の番号（0 が最下段、PT_LEVELS - 1 がルート）
 */
static uintptr_t pt_index(vaddr_t vaddr, int level) {
    return (vaddr >> (12 + level * PT_INDEX_BITS)) & ((1UL << PT_INDEX_BITS) - 1);
}

/**
 * @brief ページテーブルに vaddr -> paddr のマッピングを作る
 * @param root ルート（最上段）ページテーブルの先頭アドレス
 *
 * 仮想アドレスは VPN[2](9bit) / VPN[1](9bit) / VPN[0](9bit) / offset(12bit)
 * に分割される。ルートから段を下りながら、無い中間テーブルを作り、
 * 最下段にリーフを書く。
 */
void map_page(pte_t *root, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %lx", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %lx", paddr);

    // Sv39 で表せるのは39ビットの仮想アドレスだけ（上位ビットはビット38の
    // 符号拡張でなければならない）。このOSは下半分しか使わないので、
    // それを超えるアドレスは添字の計算で黙って切り捨てられる前に止める
    if (vaddr >> (12 + PT_LEVELS * PT_INDEX_BITS - 1))
        PANIC("vaddr %lx is out of the Sv39 lower half", vaddr);

    pte_t *table = root;
    for (int level = PT_LEVELS - 1; level > 0; level--) {
        pte_t *entry = &table[pt_index(vaddr, level)];
        if ((*entry & PAGE_V) == 0) {
            // 次の段のページテーブルが存在しないので作成する。
            // 中間エントリは R/W/X も A/D も立てない（立てるとリーフ扱いになる）
            paddr_t pt_paddr = alloc_pages(1);
            *entry = ((pt_paddr / PAGE_SIZE) << PTE_PPN_SHIFT) | PAGE_V;
        }
        table = (pte_t *) ((*entry >> PTE_PPN_SHIFT) * PAGE_SIZE);
    }

    // 最下段のテーブルにリーフエントリを書く。
    // A/D ビットはハードウェアが自動更新しない実装もあるため、あらかじめ立てておく
    table[pt_index(vaddr, 0)] =
        ((paddr / PAGE_SIZE) << PTE_PPN_SHIFT) | flags | PAGE_A | PAGE_D | PAGE_V;
}
