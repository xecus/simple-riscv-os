/* RAM_END と FREE_RAM_END はプラットフォームごとに異なる。
   build.sh が -L で platform/<名前>/ を検索パスに加えるので、
   ここでは対応する platform.ld が読まれる */
INCLUDE platform.ld

ENTRY(boot)

SECTIONS {
    . = 0x80200000;
    __kernel_base = .;

    .text :{
        KEEP(*(.text.boot));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.* .sdata .sdata.*);
    }

    .bss : ALIGN(4) {
        __bss = .;
        *(.bss .bss.* .sbss .sbss.*);
        __bss_end = .;
    }

    /* RISC-V の呼び出し規約はスタックポインタが16バイト境界にあることを
       要求するため、ブートスタックの末尾も16バイトに揃える */
    . = ALIGN(16);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;

    /* カーネルの直後から FREE_RAM_END までをページ割り当てに使う */
    . = ALIGN(4096);
    __free_ram = .;
    __free_ram_end = FREE_RAM_END;

    ASSERT(__free_ram_end <= RAM_END, "FREE_RAM_END is beyond the end of RAM")
    ASSERT(__free_ram_end % 4096 == 0, "FREE_RAM_END must be page aligned")
    /* 2プロセス分のイメージとページテーブル、スタックが収まる目安 */
    ASSERT(__free_ram + 8 * 1024 * 1024 <= __free_ram_end, "too little free RAM")
}
