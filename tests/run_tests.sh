#!/bin/bash
# ユニットテストと E2E テストをまとめて実行する（Linux / WSL 用）
#
# 使い方: tests/run_tests.sh [ARCH...]    例: tests/run_tests.sh rv32
#
# ユニットテストはテスト用の小さなカーネルとしてビルドし、QEMU 上で
# 実機と同じ幅・同じ特権モードで動かす（tests/unit/harness.h を参照）。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ARCHS=${*:-rv32}
CC=${CC:-clang}

FAILED=0

run_unit_image() {
    local arch=$1 name=$2
    shift 2
    local out="$ROOT/build/unit-$arch"
    local elf="$out/$name.elf"
    local log="$out/$name.log"
    local target qemu

    # build.sh の設定と揃えること
    case "$arch" in
        rv32)
            target=riscv32-unknown-elf
            qemu=(qemu-system-riscv32 -bios "$ROOT/opensbi-riscv32-generic-fw_dynamic.bin")
            ;;
        rv64)
            target=riscv64-unknown-elf
            qemu=(qemu-system-riscv64 -bios default)
            ;;
        *)
            echo "unknown ARCH: $arch" >&2
            return 1
            ;;
    esac

    mkdir -p "$out"
    echo "[BUILD] unit/$name ($arch)"
    if ! $CC -std=c11 -O2 -g3 -Wall -Wextra --target=$target -mcmodel=medany -fno-stack-protector \
            -ffreestanding -nostdlib -I"$ROOT" -I"$ROOT/tests/unit" \
            -Wl,-T"$ROOT/tests/unit/test.ld" -o "$elf" \
            "$ROOT/tests/unit/harness.c" "$ROOT/sbi.c" "$@"; then
        echo "[FAIL] unit/$name ($arch): build failed"
        return 1
    fi

    timeout 60 "${qemu[@]}" -machine virt -nographic -serial stdio -monitor none \
        --no-reboot -kernel "$elf" < /dev/null > "$log" 2>&1

    # OpenSBI のバナーを除き、テストの出力だけを表示する
    sed -n '/^=== unit tests/,$p' "$log" | grep -v '^\[RUN \]'

    if [ "$(grep -c '^ALL TESTS PASSED' "$log")" != 1 ]; then
        echo "[FAIL] unit/$name ($arch): see $log"
        return 1
    fi
}

for ARCH in $ARCHS; do
    echo "===== $ARCH ====="

    run_unit_image "$ARCH" test_kernel "$ROOT/tests/unit/test_kernel.c" \
        "$ROOT/common.c" "$ROOT/memory.c" "$ROOT/process.c" "$ROOT/exception.c" \
        || FAILED=1

    run_unit_image "$ARCH" test_user "$ROOT/tests/unit/test_user.c" \
        || FAILED=1

    ARCH=$ARCH python3 "$ROOT/tests/e2e/e2e.py" || FAILED=1
done

if [ $FAILED -ne 0 ]; then
    echo "SOME TESTS FAILED"
    exit 1
fi
echo "ALL TESTS PASSED ($ARCHS)"
