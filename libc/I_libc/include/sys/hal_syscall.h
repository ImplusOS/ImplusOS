#pragma once

#include <stdint.h>

uint64_t hal_syscall0(uint64_t num);
uint64_t hal_syscall1(uint64_t num, uint64_t arg1);
uint64_t hal_syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
uint64_t hal_syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
uint64_t hal_syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
uint64_t hal_syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);