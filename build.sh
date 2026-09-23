#!/bin/bash
# カーネルとユーザープログラムをビルドする（QEMU は起動しない）
#
# run.sh とテスト（tests/）の両方から使う。環境変数で次を切り替えられる。
#   ARCH       rv32（既定）または rv64
#   OUT        成果物の出力先ディレクトリ（既定: リポジトリ直下）
#   USER_MAIN  ユーザープログラムの main を含むソース（既定: user.c）
#              テストでは専用のユーザープログラムに差し替える
set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)
ARCH=${ARCH:-rv32}
OUT=${OUT:-$ROOT}
USER_MAIN=${USER_MAIN:-user.c}
CC=${CC:-clang}
OBJCOPY=${OBJCOPY:-llvm-objcopy}

case "$ARCH" in
    rv32)
        TARGET=riscv32-unknown-elf
        ELF_FORMAT=elf32-littleriscv
        ;;
    rv64)
        TARGET=riscv64-unknown-elf
        ELF_FORMAT=elf64-littleriscv
        ;;
    *)
        echo "unknown ARCH: $ARCH" >&2
        exit 1
        ;;
esac

# -mcmodel=medany: アドレスを PC 相対で作る。既定の medlow は lui で絶対番地を
# 作るため、RV64 では 0x80000000 以上のアドレス（カーネルの配置先）が
# 符号拡張されて 0xffffffff80000000 のような値になってしまう
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=$TARGET -mcmodel=medany -fno-stack-protector -ffreestanding -nostdlib -I$ROOT"

mkdir -p "$OUT"

# ユーザープログラムをビルドし、カーネルに埋め込めるオブジェクトへ変換
$CC $CFLAGS -Wl,-T"$ROOT/user.ld" -Wl,-Map="$OUT/shell.map" -o "$OUT/shell.elf" \
    "$ROOT/usys.c" "$ROOT/ulib.c" "$ROOT/$USER_MAIN"
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary "$OUT/shell.elf" "$OUT/shell.bin"

# -Ibinary が作るシンボル名（_binary_shell_bin_start など）は入力ファイルの
# パスから決まる。カーネルが参照する名前に揃えるため、出力先で実行する
(cd "$OUT" && $OBJCOPY -Ibinary -O$ELF_FORMAT shell.bin shell.bin.o)

# カーネルをビルド
$CC $CFLAGS -Wl,-T"$ROOT/kernel.ld" -Wl,-Map="$OUT/kernel.map" -o "$OUT/kernel.elf" \
    "$ROOT/kernel.c" "$ROOT/common.c" "$ROOT/sbi.c" "$ROOT/exception.c" \
    "$ROOT/memory.c" "$ROOT/process.c" "$OUT/shell.bin.o"
