#include "Syscall_Main.h"
#include "Syscall_File.h"
#include "../ProcessManager/ProcessManager.h"
#include "../Timer/Timer.h"

#include <stddef.h>
#include <stdint.h>

int64_t syscall_getuid(void)  { return 0; }
int64_t syscall_geteuid(void) { return 0; }
int64_t syscall_getgid(void)  { return 0; }
int64_t syscall_getegid(void) { return 0; }

int64_t syscall_gettid(void)
{
    return (int64_t)current_pid_get();
}

static int32_t g_tid_address_pid = -1;
static uint64_t g_tid_address_ptr = 0;

int64_t syscall_set_tid_address(uint64_t tidptr)
{
    g_tid_address_pid = current_pid_get();
    g_tid_address_ptr = tidptr;
    return (int64_t)g_tid_address_pid;
}

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

int64_t syscall_arch_prctl(uint64_t code, uint64_t addr)
{
    switch ((int)code) {
        case ARCH_SET_FS: {
            process_set_current_fs_base(addr);
            return 0;
        }
        case ARCH_GET_FS: {
            uint64_t *out = (uint64_t *)(uintptr_t)addr;
            if (out && process_user_buffer_is_valid(out, sizeof(uint64_t))) {
                *out = process_get_current_fs_base();
            }
            return 0;
        }
        default:
            return -22;
    }
}

int64_t syscall_prlimit64(uint64_t pid, uint64_t resource,
                          uint64_t new_rlim, uint64_t old_rlim)
{
    (void)pid;
    (void)resource;
    (void)new_rlim;

    if (old_rlim != 0) {
        struct { uint64_t rlim_cur; uint64_t rlim_max; } *rlim =
            (void *)(uintptr_t)old_rlim;
        if (process_user_buffer_is_valid(rlim, sizeof(*rlim))) {
            rlim->rlim_cur = 0x7FFFFFFFFFFFFFFFULL;
            rlim->rlim_max = 0x7FFFFFFFFFFFFFFFULL;
        }
    }
    return 0;
}

int64_t syscall_getrandom(uint64_t buf_ptr, uint64_t buflen, uint64_t flags)
{
    (void)flags;

    if (buflen == 0) return 0;

    uint8_t *buf = (uint8_t *)(uintptr_t)buf_ptr;
    if (!process_user_buffer_is_valid(buf, buflen)) {
        return -14;
    }

    static uint64_t prng_state = 0;
    if (prng_state == 0) {
        prng_state = timer_ticks() ^ 0x5DEECE66DULL;
    }

    for (uint64_t i = 0; i < buflen; ++i) {
        prng_state = prng_state * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (uint8_t)(prng_state >> 33);
    }

    return (int64_t)buflen;
}

typedef struct {
    uint64_t iov_base;
    uint64_t iov_len;
} iovec_t;

int64_t syscall_readv(int32_t fd, uint64_t iov_ptr, int32_t iovcnt)
{
    if (iovcnt <= 0 || iov_ptr == 0) return -22;

    const iovec_t *iov = (const iovec_t *)(uintptr_t)iov_ptr;
    if (!process_user_buffer_is_valid(iov, (uint64_t)iovcnt * sizeof(iovec_t))) {
        return -14;
    }

    int64_t total = 0;
    for (int32_t i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_len == 0) continue;
        void *buf = (void *)(uintptr_t)iov[i].iov_base;
        if (!process_user_buffer_is_valid(buf, iov[i].iov_len)) {
            return -14;
        }
        int64_t n = syscall_file_read(fd, (uint8_t *)buf, iov[i].iov_len);
        if (n < 0) {
            return total > 0 ? total : n;
        }
        total += n;
        if ((uint64_t)n < iov[i].iov_len) break;
    }
    return total;
}

int64_t syscall_writev(int32_t fd, uint64_t iov_ptr, int32_t iovcnt)
{
    if (iovcnt <= 0 || iov_ptr == 0) return -22;

    const iovec_t *iov = (const iovec_t *)(uintptr_t)iov_ptr;
    if (!process_user_buffer_is_valid(iov, (uint64_t)iovcnt * sizeof(iovec_t))) {
        return -14;
    }

    int64_t total = 0;
    for (int32_t i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_len == 0) continue;
        const void *buf = (const void *)(uintptr_t)iov[i].iov_base;
        if (!process_user_buffer_is_valid(buf, iov[i].iov_len)) {
            return -14;
        }
        int64_t n = syscall_file_write(fd, (const uint8_t *)buf, iov[i].iov_len);
        if (n < 0) {
            return total > 0 ? total : n;
        }
        total += n;
        if ((uint64_t)n < iov[i].iov_len) break;
    }
    return total;
}

int64_t syscall_ftruncate(int32_t fd, int64_t length)
{
    (void)fd;
    (void)length;
    return 0;
}

int64_t syscall_fchmod(int32_t fd, uint32_t mode)
{
    (void)fd;
    (void)mode;
    return 0;
}

int64_t syscall_rename(uint64_t oldpath, uint64_t newpath)
{
    (void)oldpath;
    (void)newpath;
    return -38;
}

int64_t syscall_access(uint64_t pathname, uint64_t mode)
{
    (void)pathname;
    (void)mode;
    return 0;
}

int64_t syscall_set_robust_list(uint64_t head, uint64_t len)
{
    (void)head;
    (void)len;
    return 0;
}

int64_t syscall_ioctl_ex(int32_t fd, uint64_t request, uint64_t arg)
{
    (void)fd;
    (void)request;
    (void)arg;
    return 0;
}
int64_t syscall_fcntl_ex(int32_t fd, int32_t cmd, uint64_t arg)
{
    (void)fd;
    (void)arg;

    switch (cmd) {
        case 1:
            return 0;
        case 2:
            return 0;
        case 3:
            return 0;
        case 4:
            return 0;
        default:
            return 0;
    }
}
