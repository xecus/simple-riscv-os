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
 * @brief Sv32 の2段ページテーブルに vaddr -> paddr のマッピングを作る
 * @param table1 1段目（ルート）ページテーブルの先頭アドレス
 *
 * 仮想アドレスは VPN[1](10bit) / VPN[0](10bit) / offset(12bit) に分割される。
 * 各テーブルは 1024エントリ x 4バイト = ちょうど1ページに収まる。
 */
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) {
        // 2段目のページテーブルが存在しないので作成する。
        // 中間エントリは R/W/X も A/D も立てない（立てるとリーフ扱いになる）
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // 2段目のページテーブルにエントリを追加する。
    // A/D ビットはハードウェアが自動更新しない実装もあるため、あらかじめ立てておく
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_A | PAGE_D | PAGE_V;
}
