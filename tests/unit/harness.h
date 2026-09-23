/**
 * @file harness.h
 * @brief ベアメタルで動く最小のユニットテスト基盤
 *
 * テストはホスト PC ではなく、QEMU 上でカーネルと同じ条件（同じ
 * ターゲット、同じレジスタ幅、同じ特権モード）で実行する。
 * RV32 と RV64 で型の幅やページテーブルの形式が変わるため、
 * 実機と同じ幅で動かさないと移行前後の比較にならないからである。
 *
 * 結果は SBI コンソールへ出力し、最後に SBI Shutdown で QEMU を終了させる。
 * 出力の末尾が "ALL TESTS PASSED" なら成功（tests/run_tests.sh が判定する）。
 *
 * テスト対象の printf / putchar と名前が衝突しないよう、
 * このハーネスの関数はすべて t_ で始める。
 */
#pragma once

void t_puts(const char *s);
void t_putdec(long value);
void t_puthex(unsigned long value);

void t_check(int ok, const char *expr, const char *file, int line);
void t_check_eq(unsigned long actual, unsigned long expected,
                const char *expr, const char *file, int line);
void t_check_str(const char *actual, const char *expected,
                 const char *expr, const char *file, int line);
void t_run(const char *name, void (*fn)(void));

// 失敗しても後続のチェックは続ける（1回の実行で失敗箇所をまとめて見るため）
#define CHECK(cond)          t_check(!!(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected)                                             \
    t_check_eq((unsigned long) (actual), (unsigned long) (expected),           \
               #actual, __FILE__, __LINE__)
#define CHECK_STR(actual, expected)                                            \
    t_check_str((actual), (expected), #actual, __FILE__, __LINE__)
#define RUN(test)            t_run(#test, test)

// 各テストファイルが1つだけ定義する。ここから RUN() で個々のテストを呼ぶ
void run_all_tests(void);

// 続行できない状態を報告して即座に終了する（モックの入力切れなど）
void t_abort(const char *reason) __attribute__((noreturn));

// ハーネスが既定で設定するトラップベクタ。テストが stvec を一時的に
// 差し替えた場合は、これに戻すこと
void t_unexpected_trap(void);
