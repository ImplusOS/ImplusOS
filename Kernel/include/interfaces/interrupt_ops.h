#pragma once

#include <stdint.h>

typedef struct {
    int (*configure)(const void *firmware_info);
    void (*eoi)(uint32_t vector);
    int (*route_irq)(uint32_t irq, uint32_t vector);
    void (*mask_irq)(uint32_t irq);
    void (*unmask_irq)(uint32_t irq);
    int (*using_local_timer)(void);
} interrupt_ops_t;

const interrupt_ops_t *interrupt_ops_get(void);
