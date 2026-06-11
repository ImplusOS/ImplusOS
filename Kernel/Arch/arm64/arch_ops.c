#include "interfaces/arch_ops.h"

#include "cpu/CPU.h"
#include "cpu/Exception.h"
#include "mmu/Paging_Main.h"

static void arm64_init_cpu_tables(void)
{
    arm64_exception_init();
}

static void arm64_enter_user_mode(uint64_t user_entry, uint64_t user_rsp, uint64_t address_space)
{
    paging_switch_cr3(address_space);
    __asm__ volatile(
        "msr sp_el0, %0\n"
        "msr elr_el1, %1\n"
        "mov x0, #0\n"
        "msr spsr_el1, x0\n"
        "eret\n"
        :: "r"(user_rsp), "r"(user_entry)
        : "x0", "memory");
}

static int arm64_virtualization_init(void)
{
    return -1;
}

static const arch_ops_t g_arm64_arch_ops = {
    .early_init = arm64_cpu_early_init,
    .init_cpu_tables = arm64_init_cpu_tables,
    .enable_interrupts = arm64_enable_interrupts,
    .disable_interrupts = arm64_disable_interrupts,
    .irq_save_disable = arm64_irq_save_disable,
    .irq_restore = arm64_irq_restore,
    .enter_user_mode = arm64_enter_user_mode,
    .virtualization_init = arm64_virtualization_init,
};

const arch_ops_t *arch_ops_get(void)
{
    return &g_arm64_arch_ops;
}
