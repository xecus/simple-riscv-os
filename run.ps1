# Windowsネイティブ用のビルド・実行スクリプト（run.shのPowerShell版）
#
# 必要なツール:
#   LLVM (clang / llvm-objcopy / lld)  winget install LLVM.LLVM
#   QEMU (qemu-system-riscv64)         winget install SoftwareFreedomConservancy.QEMU
#
# 注意: このファイルは BOM 付き UTF-8 で保存すること。BOM が無いと
#       Windows PowerShell 5.1 が日本語を Shift_JIS として読み、構文エラーになる。
# 注意: シリアルコンソール(-serial mon:stdio)を使うため、
#       Git BashやMinTTYではなくPowerShellまたはcmdから実行すること。

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# 対象のプラットフォーム（build.sh の PLATFORM に相当。既定: qemu-virt）。
# QEMU で実行できるのは platform/<名前>/qemu.args があるものだけ。
# 実機向け（milkv-duo など）は WSL で build.sh を使ってビルドすること
$Platform = if ($env:PLATFORM) { $env:PLATFORM } else { 'qemu-virt' }
$PlatformDir = "platform/$Platform"
if (-not (Test-Path "$PlatformDir/platform.h")) {
    throw "PLATFORM=$Platform は存在しません。platform/ を確認してください。"
}
if (-not (Test-Path "$PlatformDir/qemu.args")) {
    throw "PLATFORM=$Platform は QEMU で実行できません。build.sh でビルドしてください。"
}

# 成果物の出力先（build.sh の OUT に相当。既定: build/<PLATFORM>）。
# リポジトリ直下を汚さないよう、プラットフォームごとに分ける
$OutDir = if ($env:OUT) { $env:OUT } else { "build/$Platform" }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# qemu.args から追加オプションを読む（# で始まる行は無視し、空白で分割する）。
# Windows PowerShell 5.1 は BOM の無いファイルを ANSI（日本語環境では
# Shift_JIS）として読み、日本語のコメント行が崩れるので UTF-8 を指定する
$QemuArgs = @(Get-Content -Encoding UTF8 "$PlatformDir/qemu.args" |
    Where-Object { $_ -notmatch '^\s*#' } |
    ForEach-Object { $_ -split '\s+' } |
    Where-Object { $_ })

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
$QEMU    = Find-Tool 'qemu-system-riscv64' @('C:\Program Files\qemu\qemu-system-riscv64.exe')

# -fuse-ld=lld: Windowsにはriscv64向けのGNU ldが無いためLLDを明示的に指定する
# それ以外のフラグは build.sh と揃えること（-mcmodel=medany の理由も build.sh を参照）
$CFLAGS = @(
    '-std=c11', '-O2', '-g3', '-Wall', '-Wextra',
    '--target=riscv64-unknown-elf', '-march=rv64imac_zicsr_zifencei', '-mabi=lp64',
    '-mcmodel=medany',
    '-fno-stack-protector', '-ffreestanding', '-nostdlib',
    '-fuse-ld=lld', "-I$PlatformDir"
)

# 直前のネイティブコマンドが失敗していたら中断する
function Assert-Success {
    param([string]$Step)
    if ($LASTEXITCODE -ne 0) { throw "$Step に失敗しました (exit $LASTEXITCODE)" }
}

# ユーザープログラムをビルドし、カーネルに埋め込めるオブジェクトへ変換
& $CC @CFLAGS '-Wl,-Tuser.ld' "-Wl,-Map=$OutDir/shell.map" -o "$OutDir/shell.elf" usys.c ulib.c user.c
Assert-Success 'shell.elf のビルド'

& $OBJCOPY '--set-section-flags' '.bss=alloc,contents' -O binary "$OutDir/shell.elf" "$OutDir/shell.bin"
Assert-Success 'shell.bin の生成'

# -Ibinary が作るシンボル名（_binary_shell_bin_start など）は入力ファイルの
# パスから決まる。カーネルが参照する名前に揃えるため、出力先で実行する
Push-Location $OutDir
try {
    & $OBJCOPY -Ibinary -Oelf64-littleriscv shell.bin shell.bin.o
    Assert-Success 'shell.bin.o の生成'
} finally {
    Pop-Location
}

# カーネルをビルド
& $CC @CFLAGS "-Wl,-L$PlatformDir" '-Wl,-Tkernel.ld' "-Wl,-Map=$OutDir/kernel.map" -o "$OutDir/kernel.elf" `
    kernel.c common.c sbi.c exception.c memory.c process.c "$OutDir/shell.bin.o"
Assert-Success 'kernel.elf のビルド'

# QEMUを起動。riscv64 用の OpenSBI は QEMU に同梱されているので -bios default で使う
& $QEMU -machine virt @QemuArgs -bios default -nographic -serial mon:stdio --no-reboot `
    -kernel "$OutDir/kernel.elf"
