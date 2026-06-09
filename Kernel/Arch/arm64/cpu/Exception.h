#pragma once

#include <stdint.h>

typedef struct {
    uint64_t x[31];
    uint64_t sp_el0;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
} arm64_exception_frame_t;

void arm64_exception_init(void);
void arm64_exception_dispatch(arm64_exception_frame_t *frame, uint64_t type);
void arm64_exception_vector_table(void);

