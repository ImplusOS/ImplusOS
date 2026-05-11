#include <stddef.h>
#include <stdint.h>

#include "Core/process/ProcessManager.h"
#include "Core/syscall/Syscall_File.h"
#include "Debug/serial/Serial.h"

#define LINUX_ENOSYS (-38LL)

int64_t syscall_gettid(void)
{
    return (int64_t)process_get_current_pid();
}

int64_t syscall_set_tid_address(uint64_t tidptr)
{
    (void)tidptr;
    return syscall_gettid();
}

int64_t syscall_arch_prctl(uint64_t code, uint64_t addr)
{
    (void)code;
    (void)addr;
    return LINUX_ENOSYS;
}

int64_t syscall_prlimit64(uint64_t pid, uint64_t resource, uint64_t new_limit, uint64_t old_limit)
{
    (void)pid;
    (void)resource;
    (void)new_limit;
    (void)old_limit;
    return LINUX_ENOSYS;
}

int64_t syscall_getrandom(uint64_t buffer, uint64_t length, uint64_t flags)
{
    (void)buffer;
    (void)length;
    (void)flags;
    return LINUX_ENOSYS;
}

int64_t syscall_readv(int32_t fd, uint64_t iov, int32_t iovcnt)
{
    (void)fd;
    (void)iov;
    (void)iovcnt;
    return LINUX_ENOSYS;
}

int64_t syscall_writev(int32_t fd, uint64_t iov, int32_t iovcnt)
{
    (void)fd;
    (void)iov;
    (void)iovcnt;
    return LINUX_ENOSYS;
}

int64_t syscall_ftruncate(int32_t fd, int64_t length)
{
    (void)fd;
    (void)length;
    return LINUX_ENOSYS;
}

int64_t syscall_ioctl_ex(int32_t fd, uint64_t request, uint64_t arg)
{
    (void)fd;
    (void)request;
    (void)arg;
    return LINUX_ENOSYS;
}

int64_t syscall_fcntl_ex(int32_t fd, int32_t cmd, uint64_t arg)
{
    (void)fd;
    (void)cmd;
    (void)arg;
    return LINUX_ENOSYS;
}

int64_t syscall_rt_sigaction(uint64_t signum, uint64_t act, uint64_t oldact, uint64_t sigsetsize)
{
    (void)signum;
    (void)act;
    (void)oldact;
    (void)sigsetsize;
    return 0;
}

int64_t syscall_rt_sigprocmask(uint64_t how, uint64_t set, uint64_t oldset, uint64_t sigsetsize)
{
    (void)how;
    (void)set;
    (void)oldset;
    (void)sigsetsize;
    return 0;
}

int64_t write(int fd, const void *buf, uint64_t count)
{
    if (buf == NULL) {
        return -1;
    }

    if (fd == 1 || fd == 2) {
        const char *p = (const char *)buf;
        for (uint64_t i = 0; i < count; ++i) {
            serial_write_char(p[i]);
        }
        return (int64_t)count;
    }

    return syscall_file_write(fd, buf, count);
}
