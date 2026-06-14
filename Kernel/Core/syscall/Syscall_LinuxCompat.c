#include <stddef.h>
#include <stdint.h>

#include "Core/process/ProcessManager.h"
#include "Core/syscall/Syscall_File.h"
#include "Core/syscall/Syscall_Socket.h"
#include "Core/timer/Timer.h"
#include "Core/usercopy/Usercopy.h"
#include "Crypto/SHA256.h"
#include "Debug/serial/Serial.h"
#include "kernel/status.h"

#define LINUX_EBADF  (-9LL)
#define LINUX_EFAULT (-14LL)
#define LINUX_EINVAL (-22LL)
#define LINUX_ESRCH  (-3LL)
#define LINUX_ENOTSUP (-95LL)

#define LINUX_ARCH_SET_FS 0x1002u
#define LINUX_ARCH_GET_FS 0x1003u

#define LINUX_RLIMIT_STACK  3u
#define LINUX_RLIMIT_NOFILE 7u
#define LINUX_RLIMIT_AS     9u

#define LINUX_F_DUPFD  0
#define LINUX_F_GETFD  1
#define LINUX_F_SETFD  2
#define LINUX_F_GETFL  3
#define LINUX_F_SETFL  4
#define LINUX_F_DUPFD_CLOEXEC 1030
#define LINUX_FD_CLOEXEC 1u

#define LINUX_FIONREAD 0x541Bu
#define LINUX_FIONBIO  0x5421u

typedef struct {
    uint64_t base;
    uint64_t length;
} linux_iovec_t;

typedef struct {
    uint64_t current;
    uint64_t maximum;
} linux_rlimit64_t;

typedef struct {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} linux_sigaction_t;

int64_t syscall_gettid(void)
{
    return (int64_t)process_get_current_tid();
}

int64_t syscall_set_tid_address(uint64_t tidptr)
{
    int rc = process_set_clear_child_tid(tidptr);
    if (rc < 0) return rc;
    return syscall_gettid();
}

int64_t syscall_arch_prctl(uint64_t code, uint64_t addr)
{
#if defined(__x86_64__)
    if (code == LINUX_ARCH_SET_FS) {
        process_set_current_fs_base(addr);
        return 0;
    }
    if (code == LINUX_ARCH_GET_FS) {
        uint64_t value = process_get_current_fs_base();
        return copy_to_user((void *)(uintptr_t)addr, &value, sizeof(value)) == 0u ?
            0 : LINUX_EFAULT;
    }
#else
    (void)addr;
#endif
    return LINUX_EINVAL;
}

int64_t syscall_prlimit64(uint64_t pid, uint64_t resource, uint64_t new_limit, uint64_t old_limit)
{
    int32_t current = process_get_current_pid();
    if (pid != 0u && (int32_t)pid != current) return LINUX_ESRCH;
    if (new_limit != 0u) return LINUX_ENOTSUP;

    linux_rlimit64_t limit;
    switch (resource) {
        case LINUX_RLIMIT_STACK:
            limit.current = 16ULL * 4096ULL;
            limit.maximum = limit.current;
            break;
        case LINUX_RLIMIT_NOFILE:
            limit.current = 1024u;
            limit.maximum = 1024u;
            break;
        case LINUX_RLIMIT_AS:
            limit.current = 256ULL * 1024ULL * 1024ULL;
            limit.maximum = limit.current;
            break;
        default:
            return LINUX_ENOTSUP;
    }
    if (old_limit != 0u &&
        copy_to_user((void *)(uintptr_t)old_limit, &limit, sizeof(limit)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

int64_t syscall_getrandom(uint64_t buffer, uint64_t length, uint64_t flags)
{
    if ((flags & ~3u) != 0u) return LINUX_EINVAL;
    if (length == 0u) return 0;
    if (buffer == 0u ||
        !process_user_buffer_is_valid((void *)(uintptr_t)buffer, length)) {
        return LINUX_EFAULT;
    }

    static volatile uint64_t generation;
    uint64_t produced = 0;
    while (produced < length) {
        struct {
            uint64_t ticks;
            uint64_t generation;
            uint64_t cr3;
            uint64_t stack_address;
            int32_t pid;
            int32_t tid;
        } seed;
        seed.ticks = timer_ticks();
        seed.generation = __sync_add_and_fetch(&generation, 1u);
        seed.cr3 = process_get_current_cr3();
        seed.stack_address = (uint64_t)(uintptr_t)&seed;
        seed.pid = process_get_current_pid();
        seed.tid = process_get_current_tid();

        uint8_t digest[32];
        crypto_sha256((const uint8_t *)&seed, sizeof(seed), digest);
        uint64_t chunk = length - produced;
        if (chunk > sizeof(digest)) chunk = sizeof(digest);
        if (copy_to_user((uint8_t *)(uintptr_t)buffer + produced,
                         digest, chunk) != 0u) {
            return produced != 0u ? (int64_t)produced : LINUX_EFAULT;
        }
        produced += chunk;
    }
    return (int64_t)produced;
}

int64_t syscall_readv(int32_t fd, uint64_t iov, int32_t iovcnt)
{
    if (iovcnt < 0 || iovcnt > 1024 || (iovcnt != 0 && iov == 0u)) {
        return LINUX_EINVAL;
    }
    uint8_t chunk[4096];
    uint64_t total = 0;
    for (int32_t index = 0; index < iovcnt; ++index) {
        linux_iovec_t vector;
        if (copy_from_user(&vector,
                           (const linux_iovec_t *)(uintptr_t)iov + index,
                           sizeof(vector)) != 0u) {
            return total != 0u ? (int64_t)total : LINUX_EFAULT;
        }
        if (vector.length != 0u &&
            !process_user_buffer_is_valid((void *)(uintptr_t)vector.base,
                                          vector.length)) {
            return total != 0u ? (int64_t)total : LINUX_EFAULT;
        }
        uint64_t offset = 0;
        while (offset < vector.length) {
            uint64_t want = vector.length - offset;
            if (want > sizeof(chunk)) want = sizeof(chunk);
            int64_t count = syscall_file_read(fd, chunk, want);
            if (count < 0) return total != 0u ? (int64_t)total : count;
            if (count == 0) return (int64_t)total;
            if (copy_to_user((uint8_t *)(uintptr_t)vector.base + offset,
                             chunk, (uint64_t)count) != 0u) {
                return total != 0u ? (int64_t)total : LINUX_EFAULT;
            }
            offset += (uint64_t)count;
            total += (uint64_t)count;
            if ((uint64_t)count < want) return (int64_t)total;
        }
    }
    return (int64_t)total;
}

int64_t syscall_writev(int32_t fd, uint64_t iov, int32_t iovcnt)
{
    if (iovcnt < 0 || iovcnt > 1024 || (iovcnt != 0 && iov == 0u)) {
        return LINUX_EINVAL;
    }
    uint8_t chunk[4096];
    uint64_t total = 0;
    for (int32_t index = 0; index < iovcnt; ++index) {
        linux_iovec_t vector;
        if (copy_from_user(&vector,
                           (const linux_iovec_t *)(uintptr_t)iov + index,
                           sizeof(vector)) != 0u) {
            return total != 0u ? (int64_t)total : LINUX_EFAULT;
        }
        if (vector.length != 0u &&
            !process_user_buffer_is_valid((const void *)(uintptr_t)vector.base,
                                          vector.length)) {
            return total != 0u ? (int64_t)total : LINUX_EFAULT;
        }
        uint64_t offset = 0;
        while (offset < vector.length) {
            uint64_t want = vector.length - offset;
            if (want > sizeof(chunk)) want = sizeof(chunk);
            if (copy_from_user(chunk,
                               (const uint8_t *)(uintptr_t)vector.base + offset,
                               want) != 0u) {
                return total != 0u ? (int64_t)total : LINUX_EFAULT;
            }
            int64_t count = syscall_file_write(fd, chunk, want);
            if (count < 0) return total != 0u ? (int64_t)total : count;
            offset += (uint64_t)count;
            total += (uint64_t)count;
            if ((uint64_t)count < want) return (int64_t)total;
        }
    }
    return (int64_t)total;
}

int64_t syscall_ftruncate(int32_t fd, int64_t length)
{
    if (length < 0) return LINUX_EINVAL;
    return syscall_file_truncate(fd, (uint64_t)length);
}

int64_t syscall_ioctl_ex(int32_t fd, uint64_t request, uint64_t arg)
{
    if (arg == 0u) return LINUX_EFAULT;
    if (request == LINUX_FIONBIO) {
        int32_t enabled = 0;
        if (copy_from_user(&enabled, (const void *)(uintptr_t)arg,
                           sizeof(enabled)) != 0u) {
            return LINUX_EFAULT;
        }
        int32_t flags = syscall_file_get_status_flags(fd);
        if (flags < 0) return flags;
        if (enabled) flags |= 0x0800;
        else flags &= ~0x0800;
        return syscall_file_set_status_flags(fd, (uint32_t)flags);
    }
    if (request == LINUX_FIONREAD) {
        int64_t available = syscall_file_available(fd);
        if (available < 0) available = syscall_socket_available(fd);
        if (available < 0) return available;
        int32_t value = available > INT32_MAX ? INT32_MAX : (int32_t)available;
        return copy_to_user((void *)(uintptr_t)arg, &value, sizeof(value)) == 0u ?
            0 : LINUX_EFAULT;
    }
    return LINUX_ENOTSUP;
}

int64_t syscall_fcntl_ex(int32_t fd, int32_t cmd, uint64_t arg)
{
    switch (cmd) {
        case LINUX_F_DUPFD:
            return syscall_file_dup_at_least(fd, (int32_t)arg);
        case LINUX_F_DUPFD_CLOEXEC: {
            int32_t duplicated = syscall_file_dup_at_least(fd, (int32_t)arg);
            if (duplicated >= 0) {
                int32_t rc = syscall_file_set_descriptor_flags(
                    duplicated, LINUX_FD_CLOEXEC);
                if (rc < 0) {
                    (void)syscall_file_close(duplicated);
                    return rc;
                }
            }
            return duplicated;
        }
        case LINUX_F_GETFD:
            return syscall_file_get_descriptor_flags(fd);
        case LINUX_F_SETFD:
            return syscall_file_set_descriptor_flags(fd, (uint32_t)arg);
        case LINUX_F_GETFL:
            return syscall_file_get_status_flags(fd);
        case LINUX_F_SETFL:
            return syscall_file_set_status_flags(fd, (uint32_t)arg);
        default:
            return LINUX_ENOTSUP;
    }
}

int64_t syscall_rt_sigaction(uint64_t signum, uint64_t act, uint64_t oldact, uint64_t sigsetsize)
{
    if (signum == 0u || signum >= 32u || signum == 9u || signum == 19u ||
        sigsetsize != sizeof(uint64_t)) {
        return LINUX_EINVAL;
    }
    uint64_t previous = process_signal_get_handler((int32_t)signum);
    if (previous == (uint64_t)-1) return LINUX_EINVAL;
    if (oldact != 0u) {
        linux_sigaction_t old_action = {0};
        old_action.handler = previous;
        if (copy_to_user((void *)(uintptr_t)oldact,
                         &old_action, sizeof(old_action)) != 0u) {
            return LINUX_EFAULT;
        }
    }
    if (act != 0u) {
        linux_sigaction_t action;
        if (copy_from_user(&action, (const void *)(uintptr_t)act,
                           sizeof(action)) != 0u) {
            return LINUX_EFAULT;
        }
        if (process_signal_set_handler((int32_t)signum,
                                       action.handler) == (uint64_t)-1) {
            return LINUX_EINVAL;
        }
    }
    return 0;
}

int64_t syscall_rt_sigprocmask(uint64_t how, uint64_t set, uint64_t oldset, uint64_t sigsetsize)
{
    if (sigsetsize != sizeof(uint64_t)) return LINUX_EINVAL;
    uint64_t current = process_signal_get_mask();
    if (oldset != 0u &&
        copy_to_user((void *)(uintptr_t)oldset,
                     &current, sizeof(current)) != 0u) {
        return LINUX_EFAULT;
    }
    if (set != 0u) {
        uint64_t requested;
        if (copy_from_user(&requested, (const void *)(uintptr_t)set,
                           sizeof(requested)) != 0u) {
            return LINUX_EFAULT;
        }
        switch (how) {
            case 0u: current |= requested; break;
            case 1u: current &= ~requested; break;
            case 2u: current = requested; break;
            default: return LINUX_EINVAL;
        }
        return process_signal_set_mask(current);
    }
    return 0;
}

int64_t syscall_access(const char *path, int32_t mode)
{
    if (path == NULL || (mode & ~7) != 0) return LINUX_EINVAL;
    vfs_file_t file;
    if (!vfs_find_file(path, &file)) return -2;
    if ((mode & 1) != 0) return -13;
    if ((mode & 2) != 0 &&
        (file.fs_driver == NULL || file.fs_driver->write_at == NULL)) {
        return -13;
    }
    return 0;
}

int64_t write(int fd, const void *buf, uint64_t count)
{
    if (buf == NULL) {
        return -1;
    }

    uint8_t chunk[512];
    uint64_t total = 0;

    if (fd == 1 || fd == 2) {
        while (total < count) {
            uint64_t n = count - total;
            if (n > sizeof(chunk)) {
                n = sizeof(chunk);
            }
            if (copy_from_user(chunk, (const uint8_t *)buf + total, n) != 0u) {
                return total != 0u ? (int64_t)total : -1;
            }
            for (uint64_t i = 0; i < n; ++i) {
                serial_write_char((char)chunk[i]);
            }
            total += n;
        }
        return (int64_t)count;
    }

    while (total < count) {
        uint64_t n = count - total;
        if (n > sizeof(chunk)) {
            n = sizeof(chunk);
        }
        if (copy_from_user(chunk, (const uint8_t *)buf + total, n) != 0u) {
            return total != 0u ? (int64_t)total : -1;
        }
        int64_t written = syscall_file_write(fd, chunk, n);
        if (written < 0) {
            return total != 0u ? (int64_t)total : written;
        }
        total += (uint64_t)written;
        if ((uint64_t)written < n) {
            break;
        }
    }

    return (int64_t)total;
}
