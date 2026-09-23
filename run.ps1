# Windowsネイティブ用のビルド・実行スクリプト（run.shのPowerShell版）
#
# 必要なツール:
#   LLVM (clang / llvm-objcopy / lld)  winget install LLVM.LLVM
#   QEMU (qemu-system-riscv32)         winget install SoftwareFreedomConservancy.QEMU
#
# 注意: シリアルコンソール(-serial mon:stdio)を使うため、
#       Git BashやMinTTYではなくPowerShellまたはcmdから実行すること。

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# PATH上のツールを探し、見つからなければ既定のインストール先を確認する
function Find-Tool {
    param([string]$Name, [string[]]$Fallbacks)

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($path in $Fallbacks) {
        if (Test-Path $path) { return $path }
    }
    throw "$Name が見つかりません。READMEの手順でインストールし、PATHを通してください。"
}

$CC      = Find-Tool 'clang'               @('C:\Program Files\LLVM\bin\clang.exe')
$OBJCOPY = Find-Tool 'llvm-objcopy'        @('C:\Program Files\LLVM\bin\llvm-objcopy.exe')
$QEMU    = Find-Tool 'qemu-system-riscv32' @('C:\Program Files\qemu\qemu-system-riscv32.exe')

# -fuse-ld=lld: Windowsにはriscv32向けのGNU ldが無いためLLDを明示的に指定する
$CFLAGS = @(
    '-std=c11', '-O2', '-g3', '-Wall', '-Wextra',
    '--target=riscv32-unknown-elf',
    '-fno-stack-protector', '-ffreestanding', '-nostdlib',
    '-fuse-ld=lld'
)

# 直前のネイティブコマンドが失敗していたら中断する
function Assert-Success {
    param([string]$Step)
    if ($LASTEXITCODE -ne 0) { throw "$Step に失敗しました (exit $LASTEXITCODE)" }
}

# ユーザープログラムをビルドし、カーネルに埋め込めるオブジェクトへ変換
& $CC @CFLAGS '-Wl,-Tuser.ld' '-Wl,-Map=shell.map' -o shell.elf usys.c ulib.c user.c
Assert-Success 'shell.elf のビルド'

& $OBJCOPY '--set-section-flags' '.bss=alloc,contents' -O binary shell.elf shell.bin
Assert-Success 'shell.bin の生成'

& $OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o
Assert-Success 'shell.bin.o の生成'

# カーネルをビルド
& $CC @CFLAGS '-Wl,-Tkernel.ld' '-Wl,-Map=kernel.map' -o kernel.elf `
    kernel.c common.c sbi.c exception.c memory.c process.c shell.bin.o
Assert-Success 'kernel.elf のビルド'

# QEMUを起動
# 配布物にriscv32用OpenSBIが含まれない場合に備え、ローカルにあればそれを使う
$OPENSBI = 'opensbi-riscv32-generic-fw_dynamic.bin'
if (Test-Path $OPENSBI) { $bios = $OPENSBI } else { $bios = 'default' }

& $QEMU -machine virt -bios $bios -nographic -serial mon:stdio --no-reboot `
    -kernel kernel.elf
