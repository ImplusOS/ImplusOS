#include "Exception.h"
#include "CPU.h"
#include "IDT_Main.h"
#include "Core/syscall/Syscall_Main.h"
#include "Debug/panic/Panic.h"

#define ESR_EC_SHIFT 26
#define ESR_EC_MASK  0x3FU
#define ESR_EC_SVC64 0x15U

void arm64_exception_init(void)
{
    arm64_set_exception_vector((void *)arm64_exception_vector_table);
}

void arm64_exception_dispatch(arm64_exception_frame_t *frame)
{
    uint32_t ec = (uint32_t)((frame->esr_el1 >> ESR_EC_SHIFT) & ESR_EC_MASK);
    if (ec == ESR_EC_SVC64) {
        uint64_t nr = frame->x[8];
        uint64_t result_frame[1] = {0};
        (void)syscall_dispatch((uint64_t)(uintptr_t)result_frame, nr,
                               frame->x[0], frame->x[1], frame->x[2],
                               frame->x[3], frame->x[4]);
        frame->x[0] = result_frame[0];
        return;
    }
    kernel_panic("Unhandled arm64 exception", "arm64_exception_dispatch");
}

void init_idt(void)
{
    arm64_exception_init();
}

void init_idt_per_cpu(void)
{
    arm64_exception_init();
}

void register_interrupt_handler(int vector, interrupt_handler_t handler)
{
    (void)vector;
    (void)handler;
}

void init_gdt(void)
{
}

void gdt_set_kernel_rsp0(uint64_t rsp0)
{
    (void)rsp0;
}
