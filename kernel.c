#include "kernel.h"
#include "memory.h"
#include "exception.h"
#include "process.h"

extern char __bss[], __bss_end[], __stack_top[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];

__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE)
    );
}

void handle_trap(struct trap_frame *f __attribute__((unused))) {
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);
    
    switch (scause) {
        case SCAUSE_ECALL:
            handle_syscall(f);
            user_pc += 4;
            break;
        case 12: // Instruction page fault
        case 13: // Load page fault
        case 15: // Store/AMO page fault
            {
                uint32_t vaddr = ALIGN_DOWN(stval, PAGE_SIZE);
                paddr_t paddr = alloc_pages(1);
                map_page(current_proc->page_table, vaddr, paddr, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
            }
            break;
        default:
            PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }
    
    WRITE_CSR(sepc, user_pc);
}

void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_PUTCHAR:
            putchar(f->a0);
            f->a0 = 0;
            break;
        case SYS_GETCHAR:
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }

                yield();
            }
            break;
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}

void kernel_main(void) {
 
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("Hello World\n\n");

    WRITE_CSR(stvec, (uint32_t) kernel_entry);
    //__asm__ __volatile__("unimp"); // 無効な命令
 
    //idle_proc = create_process((uint32_t) NULL);
    //idle_proc->pid = 0; // idle
    //current_proc = idle_proc;
    //proc_a = create_process((uint32_t) proc_a_entry);
    //proc_b = create_process((uint32_t) proc_b_entry);

    idle_proc = create_process2(NULL, 0);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;
    create_process2(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    yield();

    PANIC("switched to idle process");

    for (;;) {
        __asm__ __volatile__("wfi");
    }
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
