#include "interfaces/arch_ops.h"

#include "cpu/GDT_Main.h"
#include "cpu/IDT_Main.h"
#include "Core/sync/Spinlock.h"
#include "virt/VMX.h"

#include "interfaces/hal_cpu.h"

static void x86_64_early_init(void)
{
}

static void x86_64_init_cpu_tables(void)
{
    init_gdt();
    init_idt();
}

static void x86_64_enable_interrupts(void)
{
    hal_cpu_enable_interrupts();
}

static void x86_64_disable_interrupts(void)
{
    hal_cpu_disable_interrupts();
}

static void x86_64_enter_user_mode(uint64_t saved_rsp, uint64_t user_rsp, uint64_t address_space)
{
    register uint64_t rdi __asm__("rdi") = saved_rsp;
    register uint64_t rsi __asm__("rsi") = user_rsp;
    register uint64_t rax __asm__("rax") = address_space;

    __asm__ volatile(
        "mov %%rax, %%cr3 \n\t"
        "mov %%rdi, %%rsp \n\t"
        "jmp syscall_enter_user_from_frame"
        :: "r"(rdi), "r"(rsi), "r"(rax)
        : "memory"
    );
}

static int x86_64_virtualization_init(void)
{
    vmx_init();
    return 0;
}

static const arch_ops_t g_x86_64_arch_ops = {
    .early_init = x86_64_early_init,
    .init_cpu_tables = x86_64_init_cpu_tables,
    .enable_interrupts = x86_64_enable_interrupts,
    .disable_interrupts = x86_64_disable_interrupts,
    .irq_save_disable = irq_save_disable,
    .irq_restore = irq_restore,
    .enter_user_mode = x86_64_enter_user_mode,
    .virtualization_init = x86_64_virtualization_init,
};

const arch_ops_t *arch_ops_get(void)
{
    return &g_x86_64_arch_ops;
}
