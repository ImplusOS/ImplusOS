#include "Timer.h"
#include <stddef.h>
#include <stdint.h>

#include "interfaces/timer_hal.h"
#include "Core/process/ProcessManager.h"
#include "Core/window/WindowManager_Kernel.h"
#include "Drivers/Module/DriverManager.h"
#include "Drivers/Module/InputManager.h"
#include "Network/network_main.h"
#include "smp/SMP_Main.h"
#include "Platform/timer/HPET.h"

static const timer_hal_t* g_timer_hal = NULL;
static volatile uint64_t g_tick_count = 0;
static timer_callback_t g_tick_callback = NULL;
static int g_timer_initialized = 0;
static volatile uint32_t g_timer_clock_started = 0;
static volatile uint32_t g_timer_services_started = 0;
static uint32_t g_requested_hz = 0;

#define TIMER_DEFAULT_HZ 250u

static void timer_core_handler(void) {
    if (smp_get_current_cpu_id() != 0u) {
        return;
    }
    g_tick_count++;

    if (__atomic_load_n(&g_timer_clock_started, __ATOMIC_ACQUIRE) != 0u) {
        timer_callback_t cb = g_tick_callback;
        if (cb) {
            cb(g_tick_count);
        }
    }

    if (__atomic_load_n(&g_timer_services_started, __ATOMIC_ACQUIRE) != 0u) {
        process_on_timer_tick();
        input_manager_schedule_poll();
        uint32_t hotplug_interval = g_requested_hz != 0u ?
                                    g_requested_hz :
                                    TIMER_DEFAULT_HZ;
        if (hotplug_interval != 0u &&
            (g_tick_count % hotplug_interval) == 0u) {
            driver_manager_schedule_hotplug_poll();
        }
        network_stack_on_timer_tick();
        wm_kernel_on_timer();
    }
}

void timer_init(const timer_hal_t* hal) {
    if (g_timer_initialized) {
        return;
    }

    g_timer_hal = hal;
    g_requested_hz = TIMER_DEFAULT_HZ;
    (void)hpet_init();

    if (g_timer_hal && g_timer_hal->init) {
        if (g_timer_hal->set_handler) {
            g_timer_hal->set_handler(timer_core_handler);
        }
        g_timer_hal->init(g_requested_hz);
    }

    g_timer_initialized = 1;
}

void timer_start_clock(void) {
    if (!g_timer_initialized) {
        return;
    }
    if (__atomic_load_n(&g_timer_clock_started, __ATOMIC_ACQUIRE) != 0u) {
        return;
    }
    __atomic_store_n(&g_timer_clock_started, 1u, __ATOMIC_RELEASE);
}

void timer_start_services(void) {
    timer_start_clock();
    __atomic_store_n(&g_timer_services_started, 1u, __ATOMIC_RELEASE);
}

void timer_set_callback(timer_callback_t cb) {
    g_tick_callback = cb;
}

uint64_t timer_ticks(void) {
    if (g_timer_hal && g_timer_hal->get_ticks) {
        return g_timer_hal->get_ticks();
    }
    return g_tick_count;
}

uint32_t timer_hz(void) {
    return (g_requested_hz != 0) ? g_requested_hz : TIMER_DEFAULT_HZ;
}

uint64_t timer_monotonic_ns(void) {
    if (hpet_is_available()) {
        return hpet_monotonic_ns();
    }
    uint32_t hz = timer_hz();
    uint64_t ticks = timer_ticks();
    if (hz == 0u) {
        return 0u;
    }
    return (ticks / hz) * 1000000000ULL +
           ((ticks % hz) * 1000000000ULL) / hz;
}

void timer_disable_irq0(void) {
    if (g_timer_hal && g_timer_hal->disable_irq) {
        g_timer_hal->disable_irq();
    }
}

void timer_switch_lapic(void) {
    if (g_timer_hal && g_timer_hal->switch_to_local) {
        g_timer_hal->switch_to_local();
    }
}

void timer_apic_sleep_ms(uint32_t ms) {
    if (g_timer_hal && g_timer_hal->msleep) {
        g_timer_hal->msleep(ms);
    }
}
