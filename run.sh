#!/bin/bash
set -eu

# OUT が相対パスで渡されたら、下の cd より前に呼び出し元基準の絶対パスにする
if [ -n "${OUT:-}" ]; then
    case "$OUT" in
        /*) ;;
        *) OUT="$PWD/$OUT" ;;
    esac
fi

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

# 成果物の出力先。build.sh の既定と同じ規則にし、ビルドしたカーネルを
# そのまま QEMU に渡す（出力先とは別の kernel.elf を起動しないため）
OUT=${OUT:-$PWD/build/$PLATFORM}
export PLATFORM OUT

# qemu.args から追加オプションを読む。コメント行を除き、Windows で
# チェックアウトした場合の CRLF も取り除く。空白で分割させるため、
# 展開するときは引用符で囲まない
QEMU_ARGS=$(grep -v '^[[:space:]]*#' "platform/$PLATFORM/qemu.args" | tr -d '\r')

# カーネルとユーザープログラムをビルド
./build.sh

# QEMUを起動。riscv64 用の OpenSBI は QEMU に同梱されているので -bios default で使う
# shellcheck disable=SC2086
qemu-system-riscv64 -machine virt $QEMU_ARGS -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel "$OUT/kernel.elf"
