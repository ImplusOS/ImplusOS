#pragma once

#include <stdint.h>

typedef struct {
    void (*early_init)(void);
    void (*init_cpu_tables)(void);
    void (*enable_interrupts)(void);
    void (*disable_interrupts)(void);
    uint64_t (*irq_save_disable)(void);
    void (*irq_restore)(uint64_t flags);
    void (*enter_user_mode)(uint64_t saved_rsp, uint64_t user_rsp, uint64_t address_space);
    int (*virtualization_init)(void);
} arch_ops_t;

const arch_ops_t *arch_ops_get(void);
