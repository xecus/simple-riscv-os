#!/bin/bash
set -eu

cd "$(dirname "$0")"

# カーネルとユーザープログラムをビルド
./build.sh

# QEMUを起動。riscv64 用の OpenSBI は QEMU に同梱されているので -bios default で使う
qemu-system-riscv64 -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
