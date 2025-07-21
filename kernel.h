#pragma once

typedef unsigned int uint32_t;
typedef unsigned long size_t;
typedef unsigned char uint8_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define NULL ((void *)0)

#define PAGE_SIZE 4096
#define USER_BASE 0x1000000
#define SSTATUS_SPIE (1 << 5)
#define SCAUSE_ECALL 8

#define SYS_PUTCHAR 1
#define SYS_GETCHAR 2
#define ALIGN_DOWN(value, align) ((value) & ~((align) - 1))
#define PAGE_READ    1
#define PAGE_WRITE   2
#define PAGE_EXEC    4

void user_entry(void);

struct sbiret {
    long error;
    long value;
};

struct trap_frame {
    uint32_t ra, gp, tp, t0, t1, t2, t3, t4, t5, t6, a0, a1, a2, a3, a4, a5, a6, a7,
             s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, sp;
};

#define READ_CSR(reg) ({                                                       \
    unsigned long __tmp;                                                       \
    __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                      \
    __tmp;                                                                     \
})

#define WRITE_CSR(reg, val) ({                                                 \
    __asm__ __volatile__("csrw " #reg ", %0" : : "r"(val));                    \
})

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid);
void putchar(char ch);
long getchar(void);
void printf(const char *format, ...);
void *memset(void *buf, char c, size_t n);
void handle_syscall(struct trap_frame *f);

static inline int is_aligned(uint32_t value, uint32_t alignment) {
    return (value & (alignment - 1)) == 0;
}

#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)
