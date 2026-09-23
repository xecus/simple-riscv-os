#include "common.h"

// 符号付き整数を10進数で出力する（int も long もここで扱う）
static void print_signed(long value) {
    // 絶対値は符号なしで求める。最小値（INT_MIN / LONG_MIN）を符号付きのまま
    // 反転するとオーバーフローするため
    // https://github.com/nuta/operating-system-in-1000-lines/issues/64
    unsigned long magnitude = value;
    if (value < 0) {
        putchar('-');
        magnitude = -magnitude;
    }

    unsigned long divisor = 1;
    while (magnitude / divisor > 9)
        divisor *= 10;

    while (divisor > 0) {
        putchar('0' + magnitude / divisor);
        magnitude %= divisor;
        divisor /= 10;
    }
}

// 符号なし整数を、指定した桁数の16進数（ゼロ埋め）で出力する
static void print_hex(unsigned long value, int digits) {
    for (int i = digits - 1; i >= 0; i--) {
        unsigned nibble = (value >> (i * 4)) & 0xf;
        putchar("0123456789abcdef"[nibble]);
    }
}

/**
 * @brief カーネル用の簡易 printf
 *
 * 対応する変換指定子:
 * - %d / %x   : int / unsigned（%x は8桁固定）
 * - %ld / %lx : long / unsigned long（%lx は16桁固定）。
 *               アドレスや CSR の値に使う
 * - %s / %%
 * 未対応の指定子は何も出力せずに読み飛ばす。
 */
void printf(const char *fmt, ...) {
    __builtin_va_list vargs;
    __builtin_va_start(vargs, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case '\0':
                    putchar('%');
                    goto end;
                case '%':
                    putchar('%');
                    break;
                case 's': {
                    const char *s = __builtin_va_arg(vargs, const char *);
                    if (!s) s = "(null)";
                    while (*s) {
                        putchar(*s);
                        s++;
                    }
                    break;
                }
                case 'd':
                    print_signed(__builtin_va_arg(vargs, int));
                    break;
                case 'x':
                    print_hex(__builtin_va_arg(vargs, unsigned), 8);
                    break;
                case 'l':
                    fmt++;
                    if (*fmt == 'd') {
                        print_signed(__builtin_va_arg(vargs, long));
                    } else if (*fmt == 'x') {
                        print_hex(__builtin_va_arg(vargs, unsigned long), 16);
                    } else if (*fmt == '\0') {
                        putchar('%');
                        putchar('l');
                        goto end;
                    }
                    break;
            }
        } else {
            putchar(*fmt);
        }

        fmt++;
    }

end:
    __builtin_va_end(vargs);
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *buf, char c, size_t n) {
    uint8_t *p = (uint8_t *) buf;
    while (n--)
        *p++ = c;
    return buf;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while (*src)
        *d++ = *src++;
    *d = '\0';
    return dst;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2)
            break;
        s1++;
        s2++;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // 何もしない命令
}
