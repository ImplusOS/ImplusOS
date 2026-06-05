#include <sys/syscalls.h>
#include <sys/hal_syscall.h>
#include <stdint.h>

#ifndef KERNEL

uint64_t syscall0(uint64_t num)
{
    return hal_syscall0(num);
}

uint64_t syscall1(uint64_t num, uint64_t arg1)
{
    return hal_syscall1(num, arg1);
}

uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2)
{
    return hal_syscall2(num, arg1, arg2);
}

uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    return hal_syscall3(num, arg1, arg2, arg3);
}

uint64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
    return hal_syscall4(num, arg1, arg2, arg3, arg4);
}

uint64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    return hal_syscall5(num, arg1, arg2, arg3, arg4, arg5);
}

#endif
