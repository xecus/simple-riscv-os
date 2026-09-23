#!/bin/bash
set -eu

cd "$(dirname "$0")"

# このスクリプトは QEMU で実行するので、QEMU 以外向けのビルドは受け付けない。
# 実機向けは PLATFORM=milkv-duo ./build.sh のようにビルドだけ行うこと
PLATFORM=${PLATFORM:-qemu-virt}
if [ "$PLATFORM" != qemu-virt ]; then
    echo "run.sh: PLATFORM=$PLATFORM cannot run on QEMU; use build.sh instead" >&2
    exit 1
fi
export PLATFORM

# カーネルとユーザープログラムをビルド
./build.sh

# QEMUを起動。riscv64 用の OpenSBI は QEMU に同梱されているので -bios default で使う
qemu-system-riscv64 -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
