#ifndef TIMER_HAL_H
#define TIMER_HAL_H

#include <stdint.h>

typedef struct {
    void     (*init)(uint32_t hz);
    uint64_t (*get_ticks)(void);
    void     (*msleep)(uint32_t ms);
    void     (*set_handler)(void (*handler)(void));
} timer_hal_t;

#endif
