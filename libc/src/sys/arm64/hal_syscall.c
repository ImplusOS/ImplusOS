#include <sys/hal_syscall.h>
#include <stdint.h>

uint64_t hal_syscall0(uint64_t num)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0");
    __asm__ volatile (
        "svc #0"
        : "=r"(x0)
        : "r"(x8)
        : "memory"
    );
    return x0;
}

uint64_t hal_syscall1(uint64_t num, uint64_t arg1)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0") = arg1;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory"
    );
    return x0;
}

uint64_t hal_syscall2(uint64_t num, uint64_t arg1, uint64_t arg2)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0") = arg1;
    register uint64_t x1 __asm__("x1") = arg2;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1)
        : "memory"
    );
    return x0;
}

uint64_t hal_syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0") = arg1;
    register uint64_t x1 __asm__("x1") = arg2;
    register uint64_t x2 __asm__("x2") = arg3;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );
    return x0;
}

uint64_t hal_syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0") = arg1;
    register uint64_t x1 __asm__("x1") = arg2;
    register uint64_t x2 __asm__("x2") = arg3;
    register uint64_t x3 __asm__("x3") = arg4;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
        : "memory"
    );
    return x0;
}

uint64_t hal_syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0") = arg1;
    register uint64_t x1 __asm__("x1") = arg2;
    register uint64_t x2 __asm__("x2") = arg3;
    register uint64_t x3 __asm__("x3") = arg4;
    register uint64_t x4 __asm__("x4") = arg5;
    __asm__ volatile (
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
        : "memory"
    );
    return x0;
}
