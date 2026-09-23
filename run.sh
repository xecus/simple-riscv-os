#!/bin/bash
set -eu

cd "$(dirname "$0")"

# 対象のプラットフォーム（既定: qemu-virt）。QEMU で実行できるのは
# platform/<名前>/qemu.args があるものだけ。実機向け（milkv-duo など）は
# PLATFORM=milkv-duo ./build.sh のようにビルドだけ行うこと
PLATFORM=${PLATFORM:-qemu-virt}
if [ ! -d "platform/$PLATFORM" ]; then
    echo "run.sh: unknown PLATFORM '$PLATFORM' (see platform/)" >&2
    exit 1
fi
if [ ! -f "platform/$PLATFORM/qemu.args" ]; then
    echo "run.sh: PLATFORM=$PLATFORM cannot run on QEMU; use build.sh instead" >&2
    exit 1
fi
export PLATFORM

# qemu.args から追加オプションを読む。コメント行を除き、Windows で
# チェックアウトした場合の CRLF も取り除く。空白で分割させるため、
# 展開するときは引用符で囲まない
QEMU_ARGS=$(grep -v '^[[:space:]]*#' "platform/$PLATFORM/qemu.args" | tr -d '\r')

# カーネルとユーザープログラムをビルド
./build.sh

# QEMUを起動。riscv64 用の OpenSBI は QEMU に同梱されているので -bios default で使う
# shellcheck disable=SC2086
qemu-system-riscv64 -machine virt $QEMU_ARGS -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
