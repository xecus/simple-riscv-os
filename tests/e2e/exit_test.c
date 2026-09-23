/**
 * @file exit_test.c
 * @brief E2E テスト用のユーザープログラム（シャットダウン経路の確認）
 *
 * 通常の user.c は printer が終わらないため、「全プロセスが終了したら
 * 電源を切る」経路を確かめられない。このプログラムは両方の役割とも
 * すぐに終了し、あわせて次の挙動も出力で確かめられるようにする。
 *
 * - 起動引数と getpid が正しく届く
 * - イメージの外側へのアクセスがデマンドページングで成功する
 * - 同じ仮想アドレスでもプロセスごとに別の物理ページになる
 * - sleep_ms で待っている間に他のプロセスが動く
 * - exit() の明示的な呼び出しと main からの return の両方で終了できる
 *
 * 出力が1行の途中で混ざらないよう、後から出力する側は先に sleep する。
 */
#include "user.h"

// イメージ（先頭 64KB 程度）より十分後ろで、USER_LIMIT より手前のアドレス
#define PROBE ((volatile int *) 0x1400000)

void main(int arg) {
    int pid = getpid();

    if (arg == PROC_ARG_PRINTER) {
        printf("[A] pid %d arg %d\n", pid, arg);
        *PROBE = 0x1234;
        sleep_ms(300);
        printf("[A] probe %x\n", *PROBE);
        printf("[A] exit\n");
        exit();
    }

    sleep_ms(100);
    *PROBE = 0x5678;
    printf("[B] pid %d arg %d\n", pid, arg);
    printf("[B] probe %x\n", *PROBE);
    printf("[B] return\n");
}
