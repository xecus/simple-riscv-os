#include "kernel.h"
#include "memory.h"

extern char __free_ram[], __free_ram_end[];

paddr_t alloc_pages(uint32_t n) {

    static paddr_t next_paddr = (paddr_t) __free_ram;
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    //printf("[alloc_pages] next_paddr=%x\n", next_paddr);

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}

void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    //printf("[map_page] table1=0x%x, vaddr=0x%x paddr=0x%x flags=0x%x\n", table1, vaddr, paddr, flags);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    //printf("-> vpn[1]=0x%x\n", vpn1);
    if ((table1[vpn1] & PAGE_V) == 0) {
        // 1段目のページテーブルが存在しないので作成する
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V;
    }

    // 2段目のページテーブルにエントリを追加する
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
    //printf("-> vpn[0]=0x%x\n", vpn0);
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}
