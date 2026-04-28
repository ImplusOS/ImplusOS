#include "Timer.h"
#include <stddef.h>
#include <stdint.h>

#include "../IDT/IDT_Main.h"
#include "../IO/IO_Main.h"
#include "../Platform/APIC/LAPIC.h"
#include "../Platform/Interrupts/Interrupts.h"
#include "../ProcessManager/ProcessManager.h"
#include "../WindowManager/WindowManager_Kernel.h"
#include "../Debbuger/Serial/Serial.h"
#include "../Drivers/Module/DriverManager.h"
#include "../Network/Network_Main.h"
#include "../SMP/SMP_Main.h"
#include "../Drivers/RTC/RTC.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_BASE_FREQ     1193182U

static volatile uint64_t g_tick_count = 0;
static uint32_t g_timer_hz = 0;
static timer_callback_t g_tick_callback = NULL;
static int g_timer_initialized = 0;
static uint32_t g_requested_hz = 0;
static int g_using_lapic = 0;

static volatile uint64_t g_time_ns = 0;
static volatile uint64_t g_time_ns_rem = 0;
static uint64_t g_ns_add = 0;
static uint64_t g_ns_rem = 0;
static uint64_t g_ns_denom = 1;

static uint32_t g_lapic_initial_count = 0;

static uint16_t g_pit_divisor = 0;
static volatile uint64_t g_pit_interrupt_count = 0;

static void pit_set_frequency(uint32_t hz) {
    if (hz == 0) return;

    uint32_t clamped_hz = hz;
    if (clamped_hz < 10u)  clamped_hz = 10u;
    if (clamped_hz > 1000u) clamped_hz = 1000u;

    uint32_t divisor32 = PIT_BASE_FREQ / clamped_hz;
    if (divisor32 == 0u) divisor32 = 1u;
    if (divisor32 > 0xFFFFu) divisor32 = 0xFFFFu;

    uint16_t divisor = (uint16_t)divisor32;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    g_pit_divisor = divisor;
    g_timer_hz = clamped_hz;

    uint64_t ns_total = (uint64_t)divisor * 1000000000ULL;
    g_ns_add   = ns_total / PIT_BASE_FREQ;
    g_ns_rem   = ns_total % PIT_BASE_FREQ;
    g_ns_denom = PIT_BASE_FREQ;
}

static void timer_irq_handler(void) {
    // 1. Local tasks (per-CPU)
    process_on_timer_tick();

    // 2. Global tasks (BSP only)
    if (smp_get_current_cpu_id() == 0) {
        if (!g_using_lapic) {
            g_pit_interrupt_count++;
        }

        g_time_ns += g_ns_add;
        g_time_ns_rem += g_ns_rem;
        if (g_time_ns_rem >= g_ns_denom) {
            g_time_ns++;
            g_time_ns_rem -= g_ns_denom;
        }

        uint64_t next_tick_count = (g_time_ns * (uint64_t)g_requested_hz) / 1000000000ULL;
        if (next_tick_count > g_tick_count) {
            uint64_t diff = next_tick_count - g_tick_count;
            for (uint64_t i = 0; i < diff; i++) {
                g_tick_count++;
                
                driver_manager_input_usb_schedule_poll();
                network_stack_on_timer_tick();

                wm_kernel_on_timer();
                timer_callback_t cb = g_tick_callback;
                if (cb) {
                    cb(g_tick_count);
                }
            }
        }
    }
}

static uint32_t lapic_calculate_initial(uint32_t target_hz)
{
    if (!platform_interrupts_using_lapic() || g_timer_hz == 0) {
        return 0;
    }
    if (target_hz == 0) {
        return 0;
    }

    uint32_t sample_ticks = g_timer_hz / 20U;
    if (sample_ticks < 4U) {
        sample_ticks = 4U;
    }

    timer_callback_t old_cb = g_tick_callback;
    g_tick_callback = NULL;

    lapic_timer_stop();
    lapic_timer_start(VECTOR_TIMER, 0xFFFFFFFFu, 0, 16);

    uint64_t start_tick = g_pit_interrupt_count;
    while (g_pit_interrupt_count == start_tick) {
        __asm__ volatile("pause" ::: "memory");
    }
    start_tick = g_pit_interrupt_count;

    while ((g_pit_interrupt_count - start_tick) < sample_ticks) {
        __asm__ volatile("pause" ::: "memory");
    }

    uint32_t elapsed = 0xFFFFFFFFu - lapic_timer_current();
    lapic_timer_stop();

    g_tick_callback = old_cb;

    if (elapsed == 0u) {
        return 0;
    }

    uint64_t lapic_hz = ((uint64_t)elapsed * (uint64_t)g_timer_hz) / (uint64_t)sample_ticks;
    uint32_t initial_count = (uint32_t)(lapic_hz / target_hz);

    if (initial_count == 0) {
        return 0;
    }

    g_ns_add = 1000000000ULL / target_hz;
    g_ns_rem = 1000000000ULL % target_hz;
    g_ns_denom = target_hz;

    g_lapic_initial_count = initial_count;

    return initial_count;
}

void timer_init(uint32_t hz) {
    if (g_timer_initialized) {
        return;
    }

    g_requested_hz = hz;
    register_interrupt_handler(VECTOR_TIMER, timer_irq_handler);
    pit_set_frequency(hz);

    if (!platform_interrupts_using_lapic()) {
        uint8_t master_mask = inb(0x21);
        master_mask &= (uint8_t)~0x01u;
        outb(0x21, master_mask);
    }

    platform_interrupts_route_pit();

    g_timer_initialized = 1;
}

void timer_set_callback(timer_callback_t cb) {
    g_tick_callback = cb;
}

uint64_t timer_ticks(void) {
    uint32_t high1, low, high2;
    do {
        high1 = ((volatile uint32_t*)&g_tick_count)[1];
        low   = ((volatile uint32_t*)&g_tick_count)[0];
        high2 = ((volatile uint32_t*)&g_tick_count)[1];
    } while (high1 != high2);
    return ((uint64_t)high1 << 32) | low;
}

uint32_t timer_hz(void) {
    if (g_requested_hz != 0) {
        return g_requested_hz;
    }
    return (g_timer_hz != 0) ? g_timer_hz : 60;
}

void timer_disable_irq0(void) {
    uint8_t master_mask = inb(0x21);
    master_mask |= 0x01u;
    outb(0x21, master_mask);
    platform_interrupts_mask_pit(); 
}

void timer_switch_lapic(void) {
    if (g_using_lapic) {
        return;
    }
    if (!platform_interrupts_using_lapic()) {
        return;
    }
    uint32_t target = g_requested_hz ? g_requested_hz : g_timer_hz;
    if (target == 0) {
        target = 100;
    }

    uint32_t initial = lapic_calculate_initial(target);
    if (initial == 0) {
        return;
    }

    lapic_timer_start(VECTOR_TIMER, initial, 1, 16);
    timer_disable_irq0();
    g_using_lapic = 1;
}

void timer_apic_sleep_ms(uint32_t ms)
{
    if (ms == 0) return;

    if (!g_using_lapic || g_lapic_initial_count == 0 || g_requested_hz == 0) {
        uint32_t hz = g_requested_hz ? g_requested_hz : 60;
        uint64_t wait_ticks = ((uint64_t)ms * hz + 999) / 1000;
        uint64_t end = timer_ticks() + wait_ticks;
        while (timer_ticks() < end) {
            __asm__ volatile("pause" ::: "memory");
        }
        return;
    }

    uint64_t ms_ticks = ((uint64_t)ms * (uint64_t)g_lapic_initial_count * (uint64_t)g_requested_hz) / 1000ULL;
    if (ms_ticks == 0) return;

    uint32_t last = lapic_timer_current();
    uint64_t elapsed = 0;

    while (elapsed < ms_ticks) {
        uint32_t current = lapic_timer_current();
        if (current <= last) {
            elapsed += (last - current);
        } else {
            elapsed += (last + (g_lapic_initial_count - current));
        }
        last = current;
        __asm__ volatile("pause" ::: "memory");
    }
}
