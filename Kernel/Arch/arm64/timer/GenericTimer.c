#include "interfaces/timer_hal.h"
#include <stdint.h>
#include <stddef.h>
#include "Arch/arm64/interrupt/GIC.h"
#include "Arch/arm64/cpu/IDT_Main.h"
#include "Debug/serial/Serial.h"

#define CNTP_CTL_ENABLE (1 << 0)
#define CNTP_CTL_IMASK  (1 << 1)
#define CNTP_CTL_ISTATUS (1 << 2)

#define GENERIC_TIMER_IRQ 30

static void (*g_timer_callback)(void) = NULL;
static volatile uint64_t g_ticks = 0;
static uint32_t g_hz = 0;

static inline uint64_t read_cntfrq(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline void write_cntp_tval(uint32_t val) {
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"((uint64_t)val));
}

static inline void write_cntp_ctl(uint32_t val) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)val));
}

static inline uint64_t read_cntpct(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static void generic_timer_irq_handler(void) {
    uint64_t ctl;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
    if (!(ctl & CNTP_CTL_ISTATUS)) return;

    g_ticks++;
    
    uint64_t freq = read_cntfrq();
    write_cntp_tval((uint32_t)(freq / g_hz));

    if (g_timer_callback) {
        g_timer_callback();
    }
}

static void generic_timer_init(uint32_t hz) {
    serial_write_string("arm64: Initializing Generic Timer HAL\n");
    g_hz = hz;
    uint64_t freq = read_cntfrq();
    
    serial_write_string("arm64: Timer frequency detected: ");
    serial_write_uint64(freq);
    serial_write_string(" Hz\n");
    
    register_interrupt_handler(GENERIC_TIMER_IRQ, generic_timer_irq_handler);
    arm64_gic_route_irq(GENERIC_TIMER_IRQ, 0);

    write_cntp_tval((uint32_t)(freq / hz));
    write_cntp_ctl(CNTP_CTL_ENABLE);
    serial_write_string("arm64: Generic Timer enabled.\n");
}

static uint64_t generic_timer_get_ticks(void) {
    return g_ticks;
}

static void generic_timer_msleep(uint32_t ms) {
    uint64_t freq = read_cntfrq();
    uint64_t start = read_cntpct();
    uint64_t wait_ticks = (freq * ms) / 1000;
    while ((read_cntpct() - start) < wait_ticks) {
        __asm__ volatile("yield" ::: "memory");
    }
}

static void generic_timer_set_handler(void (*handler)(void)) {
    g_timer_callback = handler;
}

const timer_hal_t generic_timer_hal = {
    .init = generic_timer_init,
    .get_ticks = generic_timer_get_ticks,
    .msleep = generic_timer_msleep,
    .set_handler = generic_timer_set_handler
};
