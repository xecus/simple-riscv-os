/* Milk-V Duo と同じ物理メモリ配置（64MB DRAM）。kernel.ld から INCLUDE される。
   値は platform/milkv-duo/platform.ld と揃えること。
   QEMU は qemu.args の -m 64M で同じ容量にする。

   QEMU はデバイスツリーを DRAM の末尾付近に置くので、空き RAM と重なる
   ことがある。このカーネルはデバイスツリーを読まないので害はない */
RAM_END = 0x84000000;
FREE_RAM_END = 0x83f00000;
