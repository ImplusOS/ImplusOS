#pragma once

#include <stdint.h>

#include "interfaces/timer_hal.h"

typedef void (*timer_callback_t)(uint64_t tick);

void timer_init(const timer_hal_t* hal, uint32_t hz);
void timer_set_callback(timer_callback_t cb);
uint64_t timer_ticks(void);
uint32_t timer_hz(void);
void timer_disable_irq0(void);
void timer_switch_lapic(void);
void timer_apic_sleep_ms(uint32_t ms);