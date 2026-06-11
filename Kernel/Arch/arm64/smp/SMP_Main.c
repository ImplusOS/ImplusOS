#include "SMP_Main.h"
#include "PSCI.h"
#include "kernel/config.h"

static volatile uint32_t g_cpu_count = 1;
static volatile uint32_t g_cpu_possible = 1;
static volatile int32_t g_current_pid[64];

static inline uint64_t read_mpidr(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(mpidr));
    return mpidr;
}

void smp_init(void)
{
    g_cpu_count = 1;
    g_cpu_possible = OS_CONFIG_SMP_ENABLED ? (uint32_t)OS_CONFIG_SMP_MAX_CPUS : 1u;
    if (g_cpu_possible == 0u) {
        g_cpu_possible = 1u;
    }
    for (uint32_t i = 0; i < 64u; ++i) {
        g_current_pid[i] = -1;
    }
}

uint32_t smp_get_cpu_count(void)
{
    return g_cpu_count;
}

uint32_t smp_get_possible_cpu_count(void)
{
    return g_cpu_possible;
}

uint32_t smp_get_current_cpu_id(void)
{
    return (uint32_t)(read_mpidr() & 0xFFu);
}

void smp_tlb_shootdown(uint64_t vaddr, uint64_t pages)
{
    (void)vaddr;
    (void)pages;
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

void smp_tlb_shootdown_handler(void)
{
    __asm__ volatile("dsb ishst; tlbi vmalle1is; dsb ish; isb" ::: "memory");
}

int32_t smp_get_current_pid(void)
{
    uint32_t cpu = smp_get_current_cpu_id();
    if (cpu >= 64) return -1;
    return g_current_pid[cpu];
}

void smp_set_current_pid(int32_t pid)
{
    uint32_t cpu = smp_get_current_cpu_id();
    if (cpu < 64) g_current_pid[cpu] = pid;
}
