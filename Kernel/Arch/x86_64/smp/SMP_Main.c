#include "SMP_Main.h"
#include "cpu/GDT_Main.h"
#include "cpu/IDT_Main.h"
#include "Debug/serial/Serial.h"

#include "kernel/config.h"
#include "Platform/acpi/ACPI.h"
#include "Platform/interrupt/LAPIC.h"
#include "MemoryManagement/Memory_Main.h"
#include "cpu/IDT_Main.h"
#include "cpu/GDT_Main.h"
#include "Core/syscall/Syscall_Main.h"
#include <string.h>

#include <stdint.h>
#include <stdbool.h>

#define SMP_TRAMPOLINE_PHYS  0x8000ULL
#define SMP_SHARED_PHYS      0x9000ULL
#define AP_KERNEL_STACK_SIZE (256 * 1024)

#define AP_CR3_OFF   0
#define AP_ENTRY_OFF 8
#define AP_STACK_OFF 16
#define AP_GDTR_OFF  24
#define AP_IDTR_OFF  34

typedef struct __attribute__((packed)) {
    uint64_t cr3;
    uint64_t entry;
    uint64_t stack;
    uint16_t gdtr_limit;
    uint64_t gdtr_base;
    uint16_t idtr_limit;
    uint64_t idtr_base;
} smp_shared_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

static uint32_t g_cpu_online = 1;
static uint32_t g_cpu_possible = 1;
static uint8_t  g_cpu_apic_ids[ACPI_MAX_CPUS];
static uint32_t g_cpu_apic_count = 0;
static int32_t  g_current_pid_per_cpu[OS_CONFIG_SMP_MAX_CPUS];

volatile struct {
    volatile uint64_t vaddr;
    volatile uint64_t pages;
    volatile uint32_t ack_count;
} g_tlb_req;

extern uint8_t smp_trampoline_start[];
extern uint8_t smp_trampoline_end[];

static inline uint32_t read_lapic_id(void)
{
    if (!lapic_is_present()) return 0;
    return lapic_get_id();
}

static inline void io_wait(void)
{
    __asm__ volatile("outb %%al, $0x80" :: "a"((uint8_t)0));
}

static void smp_delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 100000; j++) {
            __asm__ volatile("pause");
        }
    }
}

static uint32_t smp_detect_possible_cpus(void)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0));

    if (eax >= 0x0Bu) {
        __asm__ volatile("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(0x0Bu), "c"(0));
        uint32_t count = ebx & 0xFFFFu;
        if (count > 0) return count;
    }

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1));
    uint32_t logical = (ebx >> 16) & 0xFFu;
    if (logical == 0) logical = 1;
    return logical;
}

void ap_entry_c(void)
{
    uint32_t lapic_id = read_lapic_id();

    uint32_t cpu_idx = 0;
    bool found = false;
    for (uint32_t i = 0; i < g_cpu_apic_count; i++) {
        if (g_cpu_apic_ids[i] == (uint8_t)lapic_id) {
            cpu_idx = i;
            found = true;
            break;
        }
    }

    if (!found) {
        while (1) { __asm__ volatile("hlt"); }
    }

    g_current_pid_per_cpu[cpu_idx] = -1;

    init_gdt();
    init_idt_per_cpu();
    syscall_init_per_cpu();

    __atomic_fetch_add(&g_cpu_online, 1u, __ATOMIC_SEQ_CST);

    __asm__ volatile("sti");
    while (1) {
        __asm__ volatile("hlt");
    }
}

static void smp_send_init_sipi_sipi(uint8_t apic_id, uint8_t trampoline_vector)
{
    lapic_send_ipi(apic_id, (1u << 15) | (1u << 14) | (5u << 8));
    smp_delay_ms(10);

    lapic_send_ipi(apic_id, (1u << 14) | (6u << 8) | trampoline_vector);
    smp_delay_ms(1);

    lapic_send_ipi(apic_id, (1u << 14) | (6u << 8) | trampoline_vector);
    smp_delay_ms(2);
}

static void smp_fill_shared(uint64_t ap_cr3, void *ap_entry, uint64_t ap_stack)
{
    smp_shared_t *sh = (smp_shared_t *)(uintptr_t)SMP_SHARED_PHYS;
    sh->cr3   = ap_cr3;
    sh->entry = (uint64_t)(uintptr_t)ap_entry;
    sh->stack = ap_stack;

    IDT_Ptr *idt_ptr = idt_get_ptr();
    sh->idtr_limit = idt_ptr->limit;
    sh->idtr_base  = idt_ptr->base;

    gdtr_t gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    sh->gdtr_limit = gdtr.limit;
    sh->gdtr_base  = gdtr.base;
}

void smp_init(void)
{
    for (uint32_t i = 0; i < OS_CONFIG_SMP_MAX_CPUS; i++) {
        g_current_pid_per_cpu[i] = -1;
    }
    g_current_pid_per_cpu[0] = -1;

    const acpi_info_t *info = acpi_get_info();
    if (info && info->cpu_count > 0) {
        g_cpu_possible = info->cpu_count;
        g_cpu_apic_count = info->cpu_count;
        for (uint32_t i = 0; i < info->cpu_count && i < ACPI_MAX_CPUS; ++i) {
            g_cpu_apic_ids[i] = info->cpu_apic_ids[i];
        }
    } else {
        g_cpu_possible = smp_detect_possible_cpus();
        g_cpu_apic_count = 0;
    }

    g_cpu_online = 1;

    if (!OS_CONFIG_SMP_ENABLED || g_cpu_possible <= 1 || !lapic_is_present()) {
        return;
    }

    uint32_t trampoline_size = (uint32_t)(smp_trampoline_end - smp_trampoline_start);
    memcpy((void *)(uintptr_t)SMP_TRAMPOLINE_PHYS, smp_trampoline_start, trampoline_size);
    __asm__ volatile("mfence" ::: "memory");
    memset((void *)(uintptr_t)SMP_SHARED_PHYS, 0, 4096);

    uint32_t bsp_lapic_id = lapic_get_id();
    uint8_t  trampoline_vector = (uint8_t)(SMP_TRAMPOLINE_PHYS >> 12);

    uint64_t bsp_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(bsp_cr3));
        __asm__ volatile("wbinvd" ::: "memory");

    uint32_t aps_started = 0;

    for (uint32_t i = 0; i < g_cpu_apic_count && i < (uint32_t)OS_CONFIG_SMP_MAX_CPUS; i++) {
        uint8_t aid = g_cpu_apic_ids[i];

        if (aid == (uint8_t)bsp_lapic_id) continue;

        uint8_t *ap_stack_base = (uint8_t *)malloc(AP_KERNEL_STACK_SIZE);
        if (!ap_stack_base) {
            continue;
        }

        uint64_t ap_stack_top = ((uint64_t)(uintptr_t)(ap_stack_base + AP_KERNEL_STACK_SIZE)) & ~0xFULL;

        smp_fill_shared(bsp_cr3, ap_entry_c, ap_stack_top);

        __atomic_thread_fence(__ATOMIC_SEQ_CST);

        smp_send_init_sipi_sipi(aid, trampoline_vector);

        uint32_t timeout = 200;
        uint32_t expected = __atomic_load_n(&g_cpu_online, __ATOMIC_ACQUIRE) + 1;
        while (timeout-- > 0) {
            if (__atomic_load_n(&g_cpu_online, __ATOMIC_ACQUIRE) >= expected) break;
            smp_delay_ms(1);
        }

        if (__atomic_load_n(&g_cpu_online, __ATOMIC_ACQUIRE) >= expected) {
            aps_started++;
        }
    }
}

uint32_t smp_get_cpu_count(void)
{
    return __atomic_load_n(&g_cpu_online, __ATOMIC_ACQUIRE);
}

uint32_t smp_get_possible_cpu_count(void)
{
    return g_cpu_possible;
}

uint32_t smp_get_current_cpu_id(void)
{
    if (!lapic_is_present()) return 0;
    uint32_t lid = lapic_get_id();
    for (uint32_t i = 0; i < g_cpu_apic_count; i++) {
        if (g_cpu_apic_ids[i] == (uint8_t)lid) return i;
    }
    return 0;
}

int32_t smp_get_current_pid(void)
{
    uint32_t cpu = smp_get_current_cpu_id();
    if (cpu >= (uint32_t)OS_CONFIG_SMP_MAX_CPUS) cpu = 0;
    return g_current_pid_per_cpu[cpu];
}

void smp_set_current_pid(int32_t pid)
{
    uint32_t cpu = smp_get_current_cpu_id();
    if (cpu >= (uint32_t)OS_CONFIG_SMP_MAX_CPUS) cpu = 0;
    g_current_pid_per_cpu[cpu] = pid;
}

void smp_tlb_shootdown(uint64_t vaddr, uint64_t pages)
{
    if (__atomic_load_n(&g_cpu_online, __ATOMIC_ACQUIRE) <= 1) {
        for (uint64_t i = 0; i < pages; i++) {
            __asm__ volatile("invlpg (%0)" :: "r"(vaddr + i * 4096ULL) : "memory");
        }
        return;
    }

    __atomic_store_n(&g_tlb_req.vaddr,     vaddr, __ATOMIC_RELAXED);
    __atomic_store_n(&g_tlb_req.pages,     pages, __ATOMIC_RELAXED);
    __atomic_store_n(&g_tlb_req.ack_count, 0u,    __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    
    lapic_send_ipi(0, (3u << 18) | (uint32_t)VECTOR_TLB_SHOOTDOWN);

    uint32_t expected = __atomic_load_n(&g_cpu_online, __ATOMIC_ACQUIRE) - 1u;
    uint32_t timeout  = 0x200000u;
    while (__atomic_load_n(&g_tlb_req.ack_count, __ATOMIC_ACQUIRE) < expected) {
        if (timeout == 0) {
            break;
        }
        timeout--;
        __asm__ volatile("pause");
    }

    for (uint64_t i = 0; i < pages; i++) {
        __asm__ volatile("invlpg (%0)" :: "r"(vaddr + i * 4096ULL) : "memory");
    }
}

void smp_tlb_shootdown_handler(void)
{
    uint64_t addr  = __atomic_load_n(&g_tlb_req.vaddr, __ATOMIC_ACQUIRE);
    uint64_t pages = __atomic_load_n(&g_tlb_req.pages, __ATOMIC_ACQUIRE);
    for (uint64_t i = 0; i < pages; i++) {
        __asm__ volatile("invlpg (%0)" :: "r"(addr + i * 4096ULL) : "memory");
    }
    __atomic_fetch_add(&g_tlb_req.ack_count, 1u, __ATOMIC_RELEASE);
    lapic_eoi();
}
