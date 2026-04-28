#pragma once
#include <stdint.h>

int64_t syscall_getuid(void);
int64_t syscall_geteuid(void);
int64_t syscall_getgid(void);
int64_t syscall_getegid(void);
int64_t syscall_gettid(void);
int64_t syscall_set_tid_address(uint64_t tidptr);
int64_t syscall_arch_prctl(uint64_t code, uint64_t addr);
int64_t syscall_prlimit64(uint64_t pid, uint64_t resource,
                          uint64_t new_rlim, uint64_t old_rlim);
int64_t syscall_getrandom(uint64_t buf, uint64_t buflen, uint64_t flags);
int64_t syscall_readv(int32_t fd, uint64_t iov_ptr, int32_t iovcnt);
int64_t syscall_writev(int32_t fd, uint64_t iov_ptr, int32_t iovcnt);
int64_t syscall_ftruncate(int32_t fd, int64_t length);
int64_t syscall_fchmod(int32_t fd, uint32_t mode);
int64_t syscall_rename(uint64_t oldpath, uint64_t newpath);
int64_t syscall_access(uint64_t pathname, uint64_t mode);
int64_t syscall_set_robust_list(uint64_t head, uint64_t len);
int64_t syscall_ioctl_ex(int32_t fd, uint64_t request, uint64_t arg);
int64_t syscall_fcntl_ex(int32_t fd, int32_t cmd, uint64_t arg);
