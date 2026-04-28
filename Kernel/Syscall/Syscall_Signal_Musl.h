#pragma once
#include <stdint.h>

int64_t syscall_rt_sigaction(uint64_t signum, uint64_t act_ptr,
                             uint64_t oldact_ptr, uint64_t sigsetsize);
int64_t syscall_rt_sigprocmask(uint64_t how, uint64_t set_ptr,
                               uint64_t oldset_ptr, uint64_t sigsetsize);
int64_t syscall_rt_sigreturn(void);
int64_t syscall_sigaltstack(uint64_t ss_ptr, uint64_t old_ss_ptr);
int64_t syscall_tkill(int32_t tid, int32_t sig);
void    syscall_signal_cleanup_process(int32_t pid);
