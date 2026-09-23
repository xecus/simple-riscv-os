/**
 * @file platform.h
 * @brief Milk-V Duo（SoC: CV1800B、CPU: T-Head C906）向けのプラットフォーム定数
 *
 * 定義すべきマクロの一覧は platform/qemu-virt/platform.h を参照。
 *
 * 【要確認】以下の値は公開資料（CV1800B の DTS、Linux の T-Head 対応）に
 * 基づくもので、まだ実機で確認していない。実機で起動したら OpenSBI の
 * 起動ログと照合すること。
 */
#pragma once

#define PLATFORM_NAME       "milkv-duo"

// CV1800B の DTS の timebase-frequency（25MHz）。
// 【要確認】OpenSBI の起動ログの "Platform Timer Device" の行と照合する
#define TIMER_FREQ_HZ       25000000u

// T-Head C906 の独自拡張 MAEE（Memory Attribute Extension）のメモリ属性。
//   ビット62: C（Cacheable）
//   ビット61: B（Bufferable）
//   ビット60: SH（Shareable）
// これを立てないページは、MAEE が有効な環境ではキャッシュ無効の
// 強い順序のメモリとして扱われ、動くが極端に遅くなる。
// Linux の _PAGE_PMA_THEAD と同じ値。
// 【要確認】ファームウェアが MAEE を有効にしていること（mxstatus.MAEE）。
// 無効な場合は予約ビットを立てることになり、ページフォルトしうる
#define PTE_ATTR_NORMAL_MEM ((1UL << 62) | (1UL << 61) | (1UL << 60))
