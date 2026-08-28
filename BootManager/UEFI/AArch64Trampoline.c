#if defined(__aarch64__)

__attribute__((naked))
__attribute__((noinline))
__attribute__((used))
void aarch64_el2_to_el1_and_call(void *boot_info, unsigned long kernel_entry)
{
    __asm__ volatile (
        "mrs  x2, hcr_el2              \n"
        "orr  x2, x2, #(1 << 31)      \n"
        "msr  hcr_el2, x2              \n"
        "mrs  x2, cptr_el2             \n"
        "bic  x2, x2, #(1 << 10)      \n"
        "msr  cptr_el2, x2             \n"
        "mov  x2, xzr                  \n"
        "movk x2, #0x0030, lsl #16    \n"
        "msr  cpacr_el1, x2            \n"
        "mrs  x2, cnthctl_el2          \n"
        "orr  x2, x2, #3              \n"
        "msr  cnthctl_el2, x2          \n"
        "msr  cntvoff_el2, xzr         \n"
        "mov  x2, #0x0830              \n"
        "movk x2, #0x30C5, lsl #16    \n"
        "msr  sctlr_el1, x2            \n"
        "msr  vbar_el1, xzr            \n"
        "mov  x2, sp                   \n"
        "msr  sp_el1, x2               \n"
        "msr  elr_el2, x1              \n"
        "mov  x2, #0x1C5               \n"
        "msr  spsr_el2, x2             \n"
        "mov  x1, xzr                  \n"
        "mov  x2, xzr                  \n"
        "isb                           \n"
        "eret                          \n"
    );
}

#endif