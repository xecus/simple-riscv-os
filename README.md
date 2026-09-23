# RISC-V OS Implementation

RISC-V 64ビットアーキテクチャ（RV64）向けのシンプルなオペレーティングシステムの実装です。
当初は RV32 向けに書かれており、将来の実機（Milk-V など）への移植を見据えて RV64 へ移行しました。

参考：https://operating-system-in-1000-lines.vercel.app/ja/

## 開発環境の設定

```bash
sudo apt update && sudo apt install -y clang llvm lld qemu-system-riscv64 python3
```

riscv64 用の OpenSBI（ファームウェア）は QEMU に同梱されているため、別途の取得は不要です。

## ビルドと実行

対象のプラットフォームは環境変数 `PLATFORM` で選びます。省略すると `qemu-virt` になります。
プラットフォームごとに異なる値（タイマ周波数、物理メモリの範囲、PTE のメモリ属性）は
`platform/<名前>/` にまとめてあります。

| PLATFORM | 対象 | QEMU での実行 | 状態 |
|---|---|---|---|
| `qemu-virt` | QEMU の virt マシン | できる | 動作確認済み |
| `qemu-c906` | QEMU の virt マシン + T-Head C906 の CPU モデル + 64MB | できる | 動作確認済み |
| `milkv-duo` | Milk-V Duo（CV1800B、64MB） | できない | ビルドのみ確認。値は公開資料に基づき、実機では未確認 |

`run.sh` と `run.ps1` は、ビルドしてから QEMU を起動します。QEMU を終了するには
`Ctrl-A` を押してから `X` を押します。

### qemu-virt

Linux / WSL:

```bash
./run.sh
```

Windows（PowerShell または cmd から実行すること）:

```powershell
.\run.ps1
```

### qemu-c906

Milk-V Duo の CPU と DRAM 容量に寄せた QEMU で動かします。

Linux / WSL:

```bash
PLATFORM=qemu-c906 ./run.sh
```

Windows（PowerShell）:

```powershell
$env:PLATFORM = 'qemu-c906'
.\run.ps1
Remove-Item Env:PLATFORM   # 環境変数はセッション中残るので、終わったら消す
```

起動時に次の警告が出ますが、QEMU の C906 モデルに由来するもので動作には影響しません。

```
qemu-system-riscv64: warning: disabling zfa extension for hart 0x0000000000000000 because privilege spec version does not match
```

`qemu-c906` は Milk-V Duo そのものではなく、Duo の CPU と DRAM 容量に寄せた
QEMU です。C906 の CPU モデルと 64MB の構成で動くことを確かめられますが、
次の2点は `milkv-duo` と異なり、実機でしか確かめられません。

- タイマ周波数は QEMU の 10MHz を使う（Duo は 25MHz）
- PTE のメモリ属性ビットを立てない（理由は次の節）

### milkv-duo

ビルドだけを行います。成果物がリポジトリ直下の QEMU 向けのものと混ざらないよう、
`OUT` で出力先を分けることを勧めます。Windows では WSL から実行してください
（`run.ps1` は QEMU で動かせるプラットフォーム専用です）。

```bash
PLATFORM=milkv-duo OUT=build/milkv-duo ./build.sh
# → build/milkv-duo/kernel.elf
```

実機へ書き込んで起動する手順は、まだ整備していません。

`milkv-duo` 向けのカーネルは QEMU の `-cpu thead-c906` でも動きません。
T-Head C906 独自のメモリ属性ビット（MAEE）を PTE に立てているためで、
QEMU はこれを予約ビットとして扱い、ページテーブルを有効にした直後に
命令ページフォルトが無限に続いて無言で止まります。実機でファームウェアが
MAEE を有効にしていない場合も同じ症状になります。

### ビルドだけ行う、QEMU を手で起動する

`build.sh` はビルドだけを行います。次の環境変数で動作を切り替えられます。

| 環境変数 | 意味 | 既定値 |
|---|---|---|
| `PLATFORM` | 対象のプラットフォーム | `qemu-virt` |
| `OUT` | 成果物の出力先 | リポジトリ直下 |
| `USER_MAIN` | ユーザープログラムの main を含むソース | `user.c` |

QEMU を手で起動するときは、`platform/<名前>/qemu.args` に書かれた追加オプションを
`-machine virt` の後に付けます。このファイルがあるプラットフォームだけが QEMU で
実行できます。qemu-c906 の例:

```bash
PLATFORM=qemu-c906 OUT=build/qemu-c906 ./build.sh
qemu-system-riscv64 -machine virt -cpu thead-c906 -m 64M -bios default \
    -nographic -serial mon:stdio --no-reboot -kernel build/qemu-c906/kernel.elf
```

## テスト

Linux / WSL（python3 が必要）:

```bash
tests/run_tests.sh
```

- **ユニットテスト**（`tests/unit/`）: テスト用の小さなカーネルとしてビルドし、
  QEMU 上で実機と同じレジスタ幅・特権モードのまま実行する。
  printf、メモリ操作、ページテーブル（仕様どおりに辿る検証と、satp を
  有効にした実機変換の両方）、プロセス生成、スケジューラ、トラップ入口の
  レジスタ退避、readline、疑似コンソールを対象とする
- **E2E テスト**（`tests/e2e/`）: 実際の OS を起動してシリアル経由で操作し、
  コンソールの応答、printer の周期、プロセス終了と自動シャットダウンを確かめる。
  `qemu-virt` と `qemu-c906` の両方で回す
- **プラットフォームのビルド確認**: QEMU で動かせない `milkv-duo` 向けに、
  リンクが通ることと空き RAM の終端が期待どおりであることを確かめる

## ファイル構成と役割

### カーネル関連ファイル

#### `kernel.h`
- **役割**: カーネルの中核定数・構造体・関数宣言を定義
- **内容**: 
  - 基本型定義（uint32_t、size_t、アドレス用の uintptr_t、レジスタ幅の reg_t 等）
  - メモリレイアウト定数（PAGE_SIZE、USER_BASE）
  - RISC-V CSR操作マクロ
  - 例外・システムコール番号定義
  - トラップフレーム構造体

#### `kernel.c`
- **役割**: メインカーネル実装
- **機能**:
  - トラップ（例外・システムコール）処理
  - ページフォルト処理とデマンドページング
  - システムコール実装（putchar、getchar）
  - カーネル初期化とブート処理

#### `process.h`
- **役割**: プロセス管理の定数と構造体定義
- **内容**:
  - プロセス構造体定義
  - プロセス状態定数
  - プロセス管理関数の宣言

#### `process.c`
- **役割**: プロセス管理機能の実装
- **機能**:
  - プロセス作成（create_idle_process、create_process2）
  - コンテキストスイッチ（switch_context）
  - スケジューリング（yield）
  - ページテーブル管理

#### `memory.h`
- **役割**: メモリ管理の定数と関数宣言
- **内容**:
  - ページテーブルエントリのフラグ定義（V/R/W/X/U/A/D）
  - SATP レジスタ定数
  - メモリ管理関数宣言、TLB無効化ヘルパー

#### `memory.c`
- **役割**: メモリ管理機能の実装
- **機能**:
  - 物理メモリページ割り当て（バンプアロケータ方式。解放は未実装）
  - Sv39 3段ページテーブルの操作

### ユーザー空間ファイル

#### `user.h`
- **役割**: ユーザープログラム用の定数・関数宣言
- **内容**:
  - システムコール関数宣言
  - ユーザープログラム用ユーティリティ関数

#### `usys.c`
- **役割**: ユーザー空間のうち命令列を直接書く部分
- **機能**:
  - プログラムの入口（start）
  - システムコールインターフェース（syscall）

#### `ulib.c`
- **役割**: ユーザープログラム向けライブラリ
- **機能**:
  - システムコールのラッパー（getchar、putchar、sleep、getpid、exit）
  - 簡易printf実装（フォーマット処理）
  - 行入力（readline）

#### `user.c`
- **役割**: ユーザープログラム本体
- **機能**:
  - 疑似コンソール（run_console）と printer（run_printer）
  - 起動引数で役割を分ける main

### 低レベル実装ファイル

#### `exception.h`
- **役割**: 例外処理の定数と関数宣言
- **内容**:
  - 例外処理関数宣言
  - アセンブリとC言語の橋渡し

#### `exception.c`
- **役割**: 低レベル例外処理（アセンブリコード）
- **機能**:
  - レジスタ保存/復元
  - カーネル・ユーザーモード切り替え
  - トラップベクタ実装

### 共通ファイル

#### `common.h`
- **役割**: 各ファイル共通の基本定義
- **内容**:
  - 基本型定義
  - マクロ定義（アライメント、組み込み関数）
  - 共通関数宣言

#### `common.c`
- **役割**: 共通ユーティリティ関数の実装
- **機能**:
  - カーネル用printf（%d %x %s %% に加え、64ビット値用の %ld %lx）
  - メモリ操作（memset、memcpy）
  - 文字列操作

#### `sbi.c`
- **役割**: OpenSBI呼び出し（sbi_call）
- **機能**:
  - ecall でファームウェアにコンソール入出力やタイマ設定を依頼する
  - 命令列を直接書く部分をここに閉じ込め、common.c を純粋な C に保つ

### ビルド関連ファイル

#### `build.sh`
- **役割**: ビルドのみを行うスクリプト（run.sh とテストから使う）
- **機能**:
  - Clangでのクロスコンパイル
  - リンカスクリプトを使用したメモリレイアウト制御
  - 環境変数 OUT で出力先、USER_MAIN でユーザープログラム、PLATFORM で
    対象プラットフォームを切り替えられる

#### `run.sh`
- **役割**: ビルド・実行スクリプト
- **機能**:
  - build.sh でビルドし、QEMUで実行する

#### `run.ps1`
- **役割**: Windowsネイティブ用のビルド・実行スクリプト（run.shのPowerShell版）
- **機能**:
  - PATH上のLLVM/QEMUを自動検出
  - `-fuse-ld=lld` でLLDを明示指定（Windowsにriscv64向けGNU ldが無いため）
  - 環境変数 PLATFORM で QEMU 向けのプラットフォームを選べる
  - BOM 付き UTF-8 で保存する（Windows PowerShell 5.1 が日本語を正しく読むため）

#### `kernel.ld`
- **役割**: カーネル用リンカスクリプト
- **機能**:
  - メモリセクション配置
  - シンボル定義（__kernel_base等）
  - 空き RAM の終端をプラットフォームの platform.ld から読み、
    物理メモリを超えないことをリンク時に確かめる

#### `platform/<名前>/`
- **役割**: プラットフォームごとに異なる値の置き場所
- **内容**:
  - `platform.h`: PLATFORM_NAME、TIMER_FREQ_HZ、PTE_ATTR_NORMAL_MEM
  - `platform.ld`: RAM_END、FREE_RAM_END
  - `qemu.args`: QEMU の追加オプション。QEMU で実行できるプラットフォームにだけ置く
  - 共通コードは #ifdef で分岐せず、build.sh が検索パス（-I と -L）で読み分ける

#### `.gitignore`
- **役割**: Gitで無視するファイル指定
- **内容**: ビルド成果物（*.elf、*.bin、*.o、*.map）

## 主要な特徴

### 1. デマンドページング
- プログラムが実際にメモリにアクセスした時にページを割り当て
- メモリ使用量の最適化と起動の高速化
- 割り当て対象はユーザー空間（USER_BASE〜USER_LIMIT）に限定。
  範囲外へのアクセスやカーネルモードで発生したページフォルトはPANICで検出する

### 2. システムコール
- RISC-V ecall命令を使用
- ユーザーモード→カーネルモードの安全な切り替え
- putchar / getchar / sleep / getpid / exit の5種類を提供

### 3. プリエンプティブマルチタスク
- SBI Timer 拡張を使い、10ms ごとにタイマ割り込みを発生させる
- 割り込みハンドラが yield() を呼び、プロセスの同意なくCPUを取り上げる
- ユーザープログラムが無限ループに入っても、他のプロセスは動作を続ける
- ユーザーモード実行中はタイマ割り込みが常に有効
  （RISC-V 仕様により、U-mode では sstatus.SIE の値が無視される）
- カーネル実行中は sstatus.SIE = 0 のため割り込みが入らず、
  トラップハンドラの多重実行を防いでいる
- 例外はアイドルループで、ここだけは明示的に sstatus.SIE を立てて
  スーパーバイザモードのまま割り込みを受ける
- 出力の排他制御は持たない。printf は1文字ごとのシステムコールで
  途中でプリエンプションされうるため、複数プロセスの出力が混ざりうる。
  コンソールへの入力中に printer の出力が割り込むのはこのため。
  根本的に解決するには、入力中は他プロセスの出力を抑止する
  コンソールロック（＝プロセス間の同期機構）が必要になる

### 4. 待機（sleep）
- 起床時刻を記録してプロセスを PROC_SLEEPING にし、CPUを手放す
- 待機中はスケジューラの候補から外れるため、CPUをまったく消費しない
- タイマ割り込みのたびに起床時刻を確認し、時刻が来たプロセスを実行可能へ戻す
- 全プロセスが待機中のときはアイドルプロセスが wfi で停止する
- 精度はタイマ割り込みの間隔（10ms）に丸められる
- ユーザー空間からは sleep(秒) / sleep_ms(ミリ秒) で呼び出す
- getchar も入力が無い間は同じ待機状態を使う。カーネルモードのまま
  yield() を回し続けるとタイマ割り込みが入らなくなるため

### 5. プロセス管理
- 簡単なラウンドロビンスケジューリング
- 各プロセス専用の仮想メモリ空間
- 起動時に同じイメージから2つのユーザープロセスを生成する
- 役割は create_process2() の起動引数で渡す。カーネルが user_entry() で
  a0 に載せ、start() がそのまま main の第1引数として渡す。
  PID の採番に依存しないので、生成順を変えても役割は入れ替わらない
- getpid() で自プロセスのIDを取得できる

### 6. プロセスの終了
- exit システムコールで PROC_EXITED になり、スケジューラの候補から永久に外れる
- main から return した場合も start() が exit() を呼ぶため同じ経路を通る
- 生きているプロセスがいなくなったら、アイドルプロセスが SBI Shutdown を
  呼んで電源を切る（QEMU は --no-reboot によりそのまま終了する）
- リソースは回収しない。alloc_pages() が解放手段を持たないバンプアロケータで
  あることと、このOSがプロセスを動的生成しない（起動時に作るだけ）ことによる。
  終了したプロセスの物理ページ・ページテーブル・カーネルスタックはリークする

### 7. 疑似コンソール
- PROC_ARG_CONSOLE を受け取ったプロセスが getchar() を軸に行入力を受け付ける
- readline() が1文字ずつ読み、エコー・バックスペース編集を行う
  （QEMU の -serial mon:stdio は端末をローモードにするため端末側のエコーが無い）
- コマンド: help / hello / pid / exit
- exit を入力すると bye を表示してプロセスが終了する。その後も printer は動き続ける

### 8. 例外処理
- ページフォルト、システムコール、タイマ割り込み、未対応例外の処理
- RISC-V特権アーキテクチャに準拠

## 技術仕様

- **アーキテクチャ**: RISC-V 64ビット（RV64IMAC、LP64）
- **仮想メモリ**: Sv39（3段、4KB ページング）
- **コードモデル**: medany（カーネルを 0x80200000 に置くため。medlow では符号拡張で壊れる）
- **特権レベル**: User mode (U) + Supervisor mode (S)  
- **ページサイズ**: 4KB
- **ユーザー空間**: 0x1000000 (16MB) 〜 0x1800000 (24MB)
- **空き RAM**: カーネルの直後から platform.ld の FREE_RAM_END まで（qemu-virt では約64MB）
- **タイムスライス**: 10ms（タイマ割り込みの間隔）
- **最大プロセス数**: 8（アイドルプロセスを含む）
- **ページテーブル**: カーネル領域のマッピングは全プロセスで共有（プロセスごとに複製するのはルートテーブルの1ページだけで、下位の段は共有する）
