#!/bin/bash
set -eu

cd "$(dirname "$0")"

QEMU=qemu-system-riscv32
OPENSBI=opensbi-riscv32-generic-fw_dynamic.bin

# カーネルとユーザープログラムをビルド
./build.sh

# Ubuntu版QEMUにはriscv32用のOpenSBIが同梱されていないため、無ければ取得する
if [ ! -f "$OPENSBI" ]; then
    echo "OpenSBI ($OPENSBI) をダウンロードします..."
    curl -fsSLO "https://github.com/qemu/qemu/raw/v8.0.4/pc-bios/$OPENSBI"
fi

# QEMUを起動
$QEMU -machine virt -bios "$OPENSBI" -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
