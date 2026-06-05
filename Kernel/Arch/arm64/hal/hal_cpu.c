#include "interfaces/hal_cpu.h"
#include <stdint.h>

void hal_cpu_halt(void)
{
    __asm__ volatile("wfi");
}

void hal_cpu_pause(void)
{
    __asm__ volatile("yield");
}

void hal_cpu_enable_interrupts(void)
{
    __asm__ volatile("msr daifclr, #0x2" ::: "memory");
}

void hal_cpu_disable_interrupts(void)
{
    __asm__ volatile("msr daifset, #0xf" ::: "memory");
}

uint64_t hal_cpu_save_interrupts(void)
{
    uint64_t flags;
    __asm__ volatile("mrs %0, daif; msr daifset, #0xf" : "=r"(flags) :: "memory");
    return flags;
}

void hal_cpu_restore_interrupts(uint64_t state)
{
    __asm__ volatile("msr daif, %0" :: "r"(state) : "memory");
}

void hal_mmu_invalidate_tlb(uintptr_t addr)
{
    __asm__ volatile("dsb ish; tlbi vaae1is, %0; dsb ish; isb" :: "r"(addr >> 12) : "memory");
}

uint64_t hal_cpu_read_cr(int reg)
{
    uint64_t val = 0;
    switch (reg) {
        case 0: __asm__ volatile("mrs %0, sctlr_el1" : "=r"(val)); break;
        case 3: __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(val)); break;
        default: break;
    }
    return val;
}

void hal_cpu_write_cr(int reg, uint64_t value)
{
    switch (reg) {
        case 0: __asm__ volatile("msr sctlr_el1, %0; isb" :: "r"(value) : "memory"); break;
        case 3: __asm__ volatile("msr ttbr0_el1, %0; isb" :: "r"(value) : "memory"); break;
        default: break;
    }
}

void hal_cpu_memory_barrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

void hal_io_delay(void)
{
    __asm__ volatile("isb" ::: "memory");
}

void hal_cpu_invalidate_caches(void)
{
    // arm64 cache invalidation is complex, usually involves iterating over sets/ways
    // For now, we can use a generic hint if available or leave as placeholder
}

void hal_arch_switch_stack(uintptr_t sp)
{
    __asm__ volatile("mov sp, %0" :: "r"(sp) : "memory");
}

uint64_t hal_cpu_get_current_el(void)
{
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    return (el >> 2) & 3u;
}

void hal_cpu_set_vbar(void *vbar)
{
    uint64_t el = hal_cpu_get_current_el();
    if (el == 1) {
        __asm__ volatile("msr VBAR_EL1, %0; isb" :: "r"(vbar) : "memory");
    } else if (el == 2) {
        __asm__ volatile("msr VBAR_EL2, %0; isb" :: "r"(vbar) : "memory");
    }
}

uint64_t hal_cpu_read_fs_base(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, TPIDR_EL0" : "=r"(val));
    return val;
}

void hal_cpu_write_fs_base(uint64_t val)
{
    __asm__ volatile("msr TPIDR_EL0, %0" :: "r"(val) : "memory");
}

void hal_cpu_save_fpu(uint8_t *state)
{
    (void)state;
}

void hal_cpu_restore_fpu(uint8_t *state)
{
    (void)state;
}
