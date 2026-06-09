#include "Timer.h"
#include <stddef.h>
#include <stdint.h>

#include "interfaces/timer_hal.h"
#include "Core/process/ProcessManager.h"
#include "Core/window/WindowManager_Kernel.h"
#include "Debug/serial/Serial.h"
#include "Drivers/Module/InputManager.h"
#include "Network/network_main.h"
#include "smp/SMP_Main.h"

static const timer_hal_t* g_timer_hal = NULL;
static volatile uint64_t g_tick_count = 0;
static timer_callback_t g_tick_callback = NULL;
static int g_timer_initialized = 0;
static uint32_t g_requested_hz = 0;

static void timer_core_handler(void) {
    process_on_timer_tick();

    if (smp_get_current_cpu_id() == 0) {
        g_tick_count++;
        
        input_manager_schedule_poll();
        network_stack_on_timer_tick();
        wm_kernel_on_timer();

        timer_callback_t cb = g_tick_callback;
        if (cb) {
            cb(g_tick_count);
        }
    }
}

void timer_init(const timer_hal_t* hal, uint32_t hz) {
    if (g_timer_initialized) {
        return;
    }

    g_timer_hal = hal;
    g_requested_hz = hz;

    if (g_timer_hal) {
        g_timer_hal->set_handler(timer_core_handler);
        g_timer_hal->init(hz);
    }

    g_timer_initialized = 1;
}

void timer_set_callback(timer_callback_t cb) {
    g_tick_callback = cb;
}

uint64_t timer_ticks(void) {
    if (g_timer_hal) {
        // We can either use HAL's get_ticks or our own g_tick_count.
        // The original Timer.c had its own g_tick_count updated in handler.
        return g_tick_count;
    }
    return 0;
}

uint32_t timer_hz(void) {
    return (g_requested_hz != 0) ? g_requested_hz : 60;
}

void timer_disable_irq0(void) {
    /* TODO: arm64 implementation of disabling timer IRQ if needed */
    // For x86_64, this was PIT specific. HAL should handle this if it's HW specific.
}

void timer_switch_lapic(void) {
    /* TODO: arm64 - this is x86_64 specific. 
       The HAL implementation for x86_64 can choose to use LAPIC. */
}

void timer_apic_sleep_ms(uint32_t ms) {
    if (g_timer_hal) {
        g_timer_hal->msleep(ms);
    }
}
