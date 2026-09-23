/* Milk-V Duo（64MB DRAM）向けの物理メモリ配置。kernel.ld から INCLUDE される。
   定義すべきシンボルの一覧は platform/qemu-virt/platform.ld を参照。

   0x80000000 から OpenSBI が置かれ、カーネルは 0x80200000 に読み込まれる
   （QEMU virt と同じ）。Duo 256M などの DRAM が大きい機種では RAM_END を
   変えたディレクトリを別に作ること */

/* DRAM は 64MB */
RAM_END = 0x84000000;

/* vendor の起動フローでは、小コア（C906 @ 700MHz）用の FreeRTOS が
   DRAM 末尾の 0x83f40000 から置かれる。余裕を持ってその手前で打ち切る。
   【要確認】実機の SDK 設定（CVIMMAP_FREERTOS_ADDR）と照合する */
FREE_RAM_END = 0x83f00000;
