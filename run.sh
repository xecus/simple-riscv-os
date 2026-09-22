#!/bin/bash
set -eu

QEMU=qemu-system-riscv32
OBJCOPY=/usr/bin/llvm-objcopy
CC=/usr/bin/clang
OPENSBI=opensbi-riscv32-generic-fw_dynamic.bin

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib"

$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf user.c
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# カーネルをビルド
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c exception.c memory.c process.c shell.bin.o

# Ubuntu版QEMUにはriscv32用のOpenSBIが同梱されていないため、無ければ取得する
if [ ! -f "$OPENSBI" ]; then
    echo "OpenSBI ($OPENSBI) をダウンロードします..."
    curl -fsSLO "https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/$OPENSBI"
fi

# QEMUを起動
$QEMU -machine virt -bios "$OPENSBI" -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
