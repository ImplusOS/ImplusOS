#include "kernel/boot_info.h"
#include "interfaces/hal_cpu.h"
#include <stdint.h>

#if defined(PLATFORM_X86_64)

void hal_cpu_halt(void)
{
    __asm__ volatile("hlt");
}

void hal_cpu_pause(void)
{
    __asm__ volatile("pause");
}

void hal_cpu_enable_interrupts(void)
{
    __asm__ volatile("sti" ::: "memory");
}

void hal_cpu_disable_interrupts(void)
{
    __asm__ volatile("cli" ::: "memory");
}

uint64_t hal_cpu_save_interrupts(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

void hal_cpu_restore_interrupts(uint64_t state)
{
    __asm__ volatile("pushq %0; popfq" :: "r"(state) : "memory", "cc");
}

void hal_mmu_invalidate_tlb(uintptr_t addr)
{
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

uint64_t hal_cpu_read_cr(int reg)
{
    uint64_t val = 0;
    switch (reg) {
        case 0: __asm__ volatile("mov %%cr0, %0" : "=r"(val)); break;
        case 2: __asm__ volatile("mov %%cr2, %0" : "=r"(val)); break;
        case 3: __asm__ volatile("mov %%cr3, %0" : "=r"(val)); break;
        case 4: __asm__ volatile("mov %%cr4, %0" : "=r"(val)); break;
        default: break;
    }
    return val;
}

void hal_cpu_write_cr(int reg, uint64_t value)
{
    switch (reg) {
        case 0: __asm__ volatile("mov %0, %%cr0" :: "r"(value) : "memory"); break;
        case 3: __asm__ volatile("mov %0, %%cr3" :: "r"(value) : "memory"); break;
        case 4: __asm__ volatile("mov %0, %%cr4" :: "r"(value) : "memory"); break;
        default: break;
    }
}

void hal_cpu_memory_barrier(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

void hal_io_delay(void)
{
    __asm__ volatile("outb %%al, $0x80" :: "a"((uint8_t)0));
}

void hal_cpu_invalidate_caches(void)
{
    __asm__ volatile("wbinvd" ::: "memory");
}

uint64_t hal_cpu_read_msr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void hal_cpu_write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(low), "d"(high) : "memory");
}

void hal_cpu_get_id(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(subleaf));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

void hal_arch_switch_stack(uintptr_t sp)
{
    __asm__ volatile("mov %0, %%rsp" :: "r"(sp) : "memory");
}

__attribute__((noreturn))
void hal_arch_switch_stack_and_jump(uintptr_t sp, void (*entry)(BOOT_INFO *), BOOT_INFO *boot_info)
{
    __asm__ volatile(
        "mov %0, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(sp), "r"(entry), "D"(boot_info)
        : "memory"
    );
    __builtin_unreachable();
}

uint64_t hal_cpu_get_current_el(void)
{
    return 0;
}

void hal_cpu_set_vbar(void *vbar)
{
    (void)vbar;
}

uint64_t hal_cpu_read_fs_base(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100U));
    return ((uint64_t)hi << 32) | lo;
}

void hal_cpu_write_fs_base(uint64_t val)
{
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile("wrmsr" :: "c"(0xC0000100U), "a"(lo), "d"(hi) : "memory");
}

void hal_cpu_save_fpu(uint8_t *state)
{
    __asm__ volatile("fxsave64 %0" : "=m"(*(uint8_t (*)[512])state) :: "memory");
}

void hal_cpu_restore_fpu(uint8_t *state)
{
    __asm__ volatile("fxrstor64 %0" :: "m"(*(uint8_t (*)[512])state) : "memory");
}

#elif defined(PLATFORM_ARM64)

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
    // TODO: set/way maintenance if needed
}

void hal_arch_switch_stack(uintptr_t sp)
{
    __asm__ volatile("mov sp, %0" :: "r"(sp) : "memory");
}

__attribute__((noreturn))
void hal_arch_switch_stack_and_jump(uintptr_t sp, void (*entry)(BOOT_INFO *), BOOT_INFO *boot_info)
{
    __asm__ volatile(
        "mov sp, %0\n"
        "mov x0, %2\n"
        "br  %1\n"
        :
        : "r"(sp), "r"(entry), "r"(boot_info)
        : "x0", "memory"
    );
    __builtin_unreachable();
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

#else
#error "Unsupported platform"
#endif
