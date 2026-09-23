#!/bin/bash
# カーネルとユーザープログラムをビルドする（QEMU は起動しない）
#
# run.sh とテスト（tests/）の両方から使う。環境変数で次を切り替えられる。
#   OUT        成果物の出力先ディレクトリ（既定: build/<PLATFORM>）
#              リポジトリ直下を汚さないよう、プラットフォームごとに分ける
#   USER_MAIN  ユーザープログラムの main を含むソース（既定: user.c）
#              テストでは専用のユーザープログラムに差し替える
#   PLATFORM   対象のプラットフォーム（既定: qemu-virt）
#              platform/ 以下のディレクトリ名を指定する（例: milkv-duo）
set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)
USER_MAIN=${USER_MAIN:-user.c}
PLATFORM=${PLATFORM:-qemu-virt}
OUT=${OUT:-$ROOT/build/$PLATFORM}
PLATFORM_DIR="$ROOT/platform/$PLATFORM"
if [ ! -f "$PLATFORM_DIR/platform.h" ] || [ ! -f "$PLATFORM_DIR/platform.ld" ]; then
    echo "build.sh: unknown PLATFORM '$PLATFORM' (see $ROOT/platform/)" >&2
    exit 1
fi
# 一般的な CC / OBJCOPY は読まない。CC=gcc などが設定された環境で
# RISC-V 向けでないツールが選ばれてしまうため。差し替えるときは専用の名前で指定する
CLANG=${CLANG:-clang}
LLVM_OBJCOPY=${LLVM_OBJCOPY:-llvm-objcopy}

# -mcmodel=medany: アドレスを PC 相対で作る。既定の medlow は lui で絶対番地を
# 作るため、0x80000000 以上のアドレス（カーネルの配置先）が符号拡張されて
# 0xffffffff80000000 のような値になってしまう。
# -march / -mabi: 使う命令セットを明示する。既定の rv64gc / lp64d のままだと
# コンパイラが浮動小数点命令やレジスタを使うことがあるが、このカーネルは
# FPU を有効化していない（sstatus.FS = 0）ので実機では不正命令例外になる。
# fence.i を使うため Zifencei、CSR 命令のため Zicsr も含める
# tests/run_tests.sh と run.ps1 のフラグもこれと揃えること
# -I$PLATFORM_DIR: kernel.h が読む platform.h をプラットフォームごとに切り替える
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv64-unknown-elf -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany -fno-stack-protector -ffreestanding -nostdlib -I$ROOT -I$PLATFORM_DIR"

mkdir -p "$OUT"

# ユーザープログラムをビルドし、カーネルに埋め込めるオブジェクトへ変換
$CLANG $CFLAGS -Wl,-T"$ROOT/user.ld" -Wl,-Map="$OUT/shell.map" -o "$OUT/shell.elf" \
    "$ROOT/usys.c" "$ROOT/ulib.c" "$ROOT/$USER_MAIN"
$LLVM_OBJCOPY --set-section-flags .bss=alloc,contents -O binary "$OUT/shell.elf" "$OUT/shell.bin"

# -Ibinary が作るシンボル名（_binary_shell_bin_start など）は入力ファイルの
# パスから決まる。カーネルが参照する名前に揃えるため、出力先で実行する
(cd "$OUT" && $LLVM_OBJCOPY -Ibinary -Oelf64-littleriscv shell.bin shell.bin.o)

# カーネルをビルド。-L は kernel.ld の INCLUDE platform.ld の検索先
$CLANG $CFLAGS -Wl,-L"$PLATFORM_DIR" -Wl,-T"$ROOT/kernel.ld" -Wl,-Map="$OUT/kernel.map" -o "$OUT/kernel.elf" \
    "$ROOT/kernel.c" "$ROOT/common.c" "$ROOT/sbi.c" "$ROOT/exception.c" \
    "$ROOT/memory.c" "$ROOT/process.c" "$OUT/shell.bin.o"
