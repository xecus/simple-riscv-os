/* ユニットテスト用カーネルのリンカスクリプト。
   kernel.ld と同じ配置にし、テスト対象（memory.c / process.c）が参照する
   シンボルを同じ名前で用意する。空きメモリだけは小さくしてある */
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

    . = ALIGN(16);
    . += 128 * 1024; /* 128KB */
    __stack_top = .;

    . = ALIGN(4096);
    __free_ram = .;
    . += 8 * 1024 * 1024; /* 8MB */
    __free_ram_end = .;
}
