#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "Core/process/ProcessManager.h"
#include "Core/syscall/Syscall_File.h"
#include "Core/syscall/Syscall_Socket.h"
#include "Core/syscall/Syscall_Epoll.h"
#include "Core/syscall/Syscall_Clock.h"
#include "Core/syscall/Syscall_Futex.h"
#include "Core/syscall/Syscall_VM.h"
#include "Core/syscall/Syscall_Main.h"
#include "Core/timer/Timer.h"
#include "Core/usercopy/Usercopy.h"
#include "Core/vfs/VFS.h"
#include "Crypto/Crypto.h"
#include "Debug/serial/Serial.h"
#include "Drivers/RTC/RTC.h"
#include "kernel/status.h"
#include "mmu/Paging_Main.h"

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

/* ------------------------------------------------------------------ */
/* Linux x86_64 syscall number table (phase 2: compatibility layer)   */
/* ------------------------------------------------------------------ */

#define LINUX_SYS_READ           0u
#define LINUX_SYS_WRITE          1u
#define LINUX_SYS_OPEN           2u
#define LINUX_SYS_CLOSE          3u
#define LINUX_SYS_FSTAT          5u
#define LINUX_SYS_LSEEK          8u
#define LINUX_SYS_MMAP           9u
#define LINUX_SYS_MPROTECT      10u
#define LINUX_SYS_MUNMAP        11u
#define LINUX_SYS_BRK           12u
#define LINUX_SYS_RT_SIGACTION  13u
#define LINUX_SYS_RT_SIGPROCMASK 14u
#define LINUX_SYS_IOCTL         16u
#define LINUX_SYS_READV         19u
#define LINUX_SYS_WRITEV        20u
#define LINUX_SYS_ACCESS        21u
#define LINUX_SYS_PIPE          22u
#define LINUX_SYS_SCHED_YIELD   24u
#define LINUX_SYS_DUP           32u
#define LINUX_SYS_DUP2          33u
#define LINUX_SYS_NANOSLEEP     35u
#define LINUX_SYS_GETPID        39u
#define LINUX_SYS_CLONE         56u
#define LINUX_SYS_FORK          57u
#define LINUX_SYS_VFORK         58u
#define LINUX_SYS_EXECVE        59u
#define LINUX_SYS_EXIT          60u
#define LINUX_SYS_WAIT4         61u
#define LINUX_SYS_KILL          62u
#define LINUX_SYS_UNAME         63u
#define LINUX_SYS_FCNTL         72u
#define LINUX_SYS_FTRUNCATE     77u
#define LINUX_SYS_GETCWD        79u
#define LINUX_SYS_CHDIR         80u
#define LINUX_SYS_RENAME        82u
#define LINUX_SYS_MKDIR         83u
#define LINUX_SYS_RMDIR         84u
#define LINUX_SYS_CREAT         85u
#define LINUX_SYS_UNLINK        87u
#define LINUX_SYS_GETTIMEOFDAY  96u
#define LINUX_SYS_GETRLIMIT     97u
#define LINUX_SYS_GETUID       102u
#define LINUX_SYS_GETGID       104u
#define LINUX_SYS_GETEUID      107u
#define LINUX_SYS_GETEGID      108u
#define LINUX_SYS_GETPPID      110u
#define LINUX_SYS_PRCTL        157u
#define LINUX_SYS_ARCH_PRCTL   158u
#define LINUX_SYS_SETRLIMIT    160u
#define LINUX_SYS_GETTID       186u
#define LINUX_SYS_TKILL        200u
#define LINUX_SYS_TIME         201u
#define LINUX_SYS_FUTEX        202u
#define LINUX_SYS_GETDENTS64   217u
#define LINUX_SYS_SET_TID_ADDRESS 218u
#define LINUX_SYS_CLOCK_GETTIME 228u
#define LINUX_SYS_CLOCK_GETRES  229u
#define LINUX_SYS_EXIT_GROUP   231u
#define LINUX_SYS_EPOLL_WAIT   232u
#define LINUX_SYS_EPOLL_CTL    233u
#define LINUX_SYS_TGKILL       234u
#define LINUX_SYS_OPENAT       257u
#define LINUX_SYS_NEWFSTATAT   262u
#define LINUX_SYS_SET_ROBUST_LIST 273u
#define LINUX_SYS_TIMERFD_CREATE 283u
#define LINUX_SYS_EVENTFD      284u
#define LINUX_SYS_TIMERFD_SETTIME 286u
#define LINUX_SYS_TIMERFD_GETTIME 287u
#define LINUX_SYS_SIGNALFD4    289u
#define LINUX_SYS_EVENTFD2     290u
#define LINUX_SYS_EPOLL_CREATE1 291u
#define LINUX_SYS_PRLIMIT64    302u
#define LINUX_SYS_GETCPU       309u
#define LINUX_SYS_GETRANDOM    318u
#define LINUX_SYS_MEMFD_CREATE 319u
#define LINUX_SYS_RSEQ         334u

#define LINUX_SYS_SOCKET        41u
#define LINUX_SYS_CONNECT       42u
#define LINUX_SYS_ACCEPT        43u
#define LINUX_SYS_SENDTO        44u
#define LINUX_SYS_RECVFROM      45u
#define LINUX_SYS_SHUTDOWN      48u
#define LINUX_SYS_BIND          49u
#define LINUX_SYS_LISTEN        50u
#define LINUX_SYS_SETSOCKOPT    54u
#define LINUX_SYS_GETSOCKOPT    55u

#define LINUX_SYS_STAT          4u
#define LINUX_SYS_LSTAT         6u

#define LINUX_MAP_SHARED       0x01u
#define LINUX_MAP_PRIVATE      0x02u
#define LINUX_MAP_FIXED        0x10u
#define LINUX_MAP_ANONYMOUS    0x20u

#define LINUX_PROT_READ        0x1u
#define LINUX_PROT_WRITE       0x2u
#define LINUX_PROT_EXEC        0x4u

#define LINUX_SEEK_SET         0u
#define LINUX_SEEK_CUR         1u
#define LINUX_SEEK_END         2u

#define LINUX_O_CREAT          0x40u
#define LINUX_O_DIRECTORY      0x10000u

#define LINUX_AT_FDCWD         (-100)
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100u

#define LINUX_WNOHANG          1u

#define LINUX_PR_SET_NAME      15u
#define LINUX_PR_GET_NAME      16u

#define LINUX_CLONE_PARENT_SETTID   0x00008000u
#define LINUX_CLONE_CHILD_CLEARTID  0x00200000u
#define LINUX_CLONE_THREAD          0x00010000u
#define LINUX_CLONE_CHILD_SETTID    0x01000000u

#define LINUX_S_IFMT   0xF000u
#define LINUX_S_IFDIR  0x4000u
#define LINUX_S_IFREG  0x8000u
#define LINUX_S_IFIFO  0x1000u

#define LINUX_AF_INET       2u
#define LINUX_SOCK_STREAM   1u
#define LINUX_SOL_SOCKET    1u
#define LINUX_SO_REUSEADDR  2u
#define LINUX_SO_KEEPALIVE  9u
#define LINUX_SO_SNDTIMEO   21u
#define LINUX_SO_RCVTIMEO   20u
#define LINUX_SHUT_RD       0
#define LINUX_SHUT_WR       1
#define LINUX_SHUT_RDWR     2
#define LINUX_EAFNOSUPPORT  (-97LL)
#define LINUX_EPROTONOSUPPORT (-93LL)
#define LINUX_ENOPROTOOPT   (-92LL)

#define LINUX_ENOSYS (-38LL)
#define LINUX_ENOMEM (-12LL)
#define LINUX_ENOENT (-2LL)
#define LINUX_EACCES (-13LL)
#define LINUX_EAGAIN (-11LL)
#define LINUX_EBUSY  (-16LL)
#define LINUX_ENOTDIR (-20LL)
#define LINUX_EISDIR (-21LL)
#define LINUX_ENOTEMPTY (-39LL)
#define LINUX_ENAMETOOLONG (-36LL)

#define LINUX_UTSNAME_LEN 65u

#define LINUX_MAX_IO_BYTES (4ULL * 1024ULL * 1024ULL)

static void linux_syscall_result(uint64_t saved_rsp, int64_t value)
{
#if defined(__x86_64__)
    uint64_t *frame = (uint64_t *)(uintptr_t)saved_rsp;
    frame[SYSCALL_FRAME_RAX] = (uint64_t)value;
#else
    (void)saved_rsp;
    (void)value;
#endif
}

static int64_t linux_copy_cstring(char *out, uint64_t capacity,
                                  const char *user_ptr)
{
    if (user_ptr == NULL) {
        return LINUX_EFAULT;
    }
    uint64_t len = 0;
    if (process_user_cstring_length(user_ptr, capacity - 1u, &len) < 0) {
        return LINUX_EFAULT;
    }
    if (copy_from_user(out, user_ptr, len + 1u) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

static int64_t linux_days_from_civil(int64_t year, unsigned month,
                                     unsigned day)
{
    year -= (month <= 2u) ? 1 : 0;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy =
        (153u * (month + (month > 2u ? 0u : 9u)) + 2u) / 5u + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t linux_realtime_seconds(void)
{
    rtc_time_t rtc;
    rtc_read_time(&rtc);
    int64_t days = linux_days_from_civil((int64_t)rtc.year,
                                         rtc.month, rtc.day);
    return days * 86400 + (int64_t)rtc.hour * 3600 +
           (int64_t)rtc.minute * 60 + (int64_t)rtc.second;
}

static int64_t linux_brk(uint64_t addr)
{
    uint64_t cursor = process_get_heap_cursor();
    if (addr == 0u) {
        return (int64_t)cursor;
    }
    if (process_set_heap_cursor(addr) < 0) {
        return (int64_t)cursor;
    }
    return (int64_t)addr;
}

static int64_t linux_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                          uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)prot;
    if (length == 0u) {
        return LINUX_EINVAL;
    }
    if ((flags & LINUX_MAP_ANONYMOUS) != 0u) {
        if ((flags & LINUX_MAP_FIXED) != 0u) {
            if (addr == 0u || addr < 0x1000u || length > 0x4000000000ULL - addr) {
                return LINUX_EINVAL;
            }
            uint64_t cr3 = process_get_current_cr3();
            if (cr3 == 0u ||
                paging_map_user_range_alloc(cr3, addr, length,
                                            PAGE_RW | PAGE_USER) < 0) {
                return LINUX_ENOMEM;
            }
            return (int64_t)addr;
        }
        void *mapped = process_user_mmap(length, flags);
        if (mapped == NULL) {
            return LINUX_ENOMEM;
        }
        return (int64_t)(uintptr_t)mapped;
    }

    vfs_file_t vf;
    if (syscall_file_get_file_info((int32_t)fd, &vf, NULL) < 0) {
        return LINUX_EBADF;
    }
    if (offset != 0u) {
        return LINUX_ENOTSUP;
    }
    uint64_t read_len = length;
    if (read_len > (uint64_t)vf.size) {
        read_len = (uint64_t)vf.size;
    }

    void *mapped;
    if ((flags & LINUX_MAP_FIXED) != 0u) {
        if (addr == 0u || addr < 0x1000u || length > 0x4000000000ULL - addr) {
            return LINUX_EINVAL;
        }
        uint64_t cr3 = process_get_current_cr3();
        if (cr3 == 0u ||
            paging_map_user_range_alloc(cr3, addr, length,
                                        PAGE_RW | PAGE_USER) < 0) {
            return LINUX_ENOMEM;
        }
        mapped = (void *)(uintptr_t)addr;
    } else {
        mapped = process_user_mmap(length, flags);
        if (mapped == NULL) {
            return LINUX_ENOMEM;
        }
    }

    uint64_t total = 0;
    uint8_t chunk[4096];
    while (total < read_len) {
        uint64_t want = read_len - total;
        if (want > sizeof(chunk)) want = sizeof(chunk);
        int64_t count = syscall_file_read((int32_t)fd, chunk, want);
        if (count <= 0) break;
        if (copy_to_user((uint8_t *)(uintptr_t)mapped + total,
                         chunk, (uint64_t)count) != 0u) {
            return LINUX_EFAULT;
        }
        total += (uint64_t)count;
    }
    return (int64_t)(uintptr_t)mapped;
}

static int64_t linux_epoll_ctl(uint64_t epfd, uint64_t op, uint64_t fd,
                               uint64_t event_ptr)
{
    if (event_ptr == 0u &&
        (op == 1u || op == 3u)) {
        return LINUX_EFAULT;
    }
    if (event_ptr != 0u &&
        !process_user_buffer_is_valid((const void *)(uintptr_t)event_ptr,
                                      sizeof(epoll_event_t))) {
        return LINUX_EFAULT;
    }
    return (int64_t)syscall_epoll_ctl((int32_t)epfd, (int32_t)op,
                                      (int32_t)fd,
                                      (const epoll_event_t *)(uintptr_t)event_ptr);
}

static int64_t linux_epoll_wait(uint64_t epfd, uint64_t events,
                                uint64_t maxevents, uint64_t timeout_ms)
{
    if (maxevents == 0u || maxevents > 4096u) {
        return LINUX_EINVAL;
    }
    if (!process_user_buffer_is_valid((const void *)(uintptr_t)events,
                                      maxevents * sizeof(epoll_event_t))) {
        return LINUX_EFAULT;
    }
    return (int64_t)syscall_epoll_wait((int32_t)epfd,
                                       (epoll_event_t *)(uintptr_t)events,
                                       (int32_t)maxevents,
                                       (int32_t)timeout_ms);
}

static int64_t linux_wait4(uint64_t pid, uint64_t status_ptr,
                           uint64_t options, uint64_t rusage)
{
    (void)rusage;
    if ((options & ~(LINUX_WNOHANG | 2u)) != 0u) {
        return LINUX_EINVAL;
    }
    if (status_ptr != 0u &&
        !process_user_buffer_is_valid((const void *)(uintptr_t)status_ptr,
                                      sizeof(int32_t))) {
        return LINUX_EFAULT;
    }
    int32_t child_status = 0;
    int32_t result = process_waitpid((int32_t)pid,
                                     status_ptr != 0u ? &child_status : NULL,
                                     (int32_t)options);
    if (result > 0 && status_ptr != 0u &&
        copy_to_user((void *)(uintptr_t)status_ptr,
                     &child_status, sizeof(child_status)) != 0u) {
        return LINUX_EFAULT;
    }
    return (int64_t)result;
}

static int64_t linux_rt_sigaction(uint64_t signum, uint64_t act,
                                  uint64_t oldact, uint64_t sigsetsize)
{
    return syscall_rt_sigaction(signum, act, oldact, sigsetsize);
}

static int64_t linux_rt_sigprocmask(uint64_t how, uint64_t set,
                                    uint64_t oldset, uint64_t sigsetsize)
{
    return syscall_rt_sigprocmask(how, set, oldset, sigsetsize);
}

static int64_t linux_gettimeofday(uint64_t tv_ptr)
{
    if (tv_ptr == 0u) {
        return 0;
    }
    if (!process_user_buffer_is_valid((const void *)(uintptr_t)tv_ptr,
                                      sizeof(int64_t) * 2u)) {
        return LINUX_EFAULT;
    }
    int64_t tv[2];
    tv[0] = linux_realtime_seconds();
    tv[1] = 0;
    if (copy_to_user((void *)(uintptr_t)tv_ptr, tv, sizeof(tv)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

static int64_t linux_uname(uint64_t uts_ptr)
{
    const uint64_t uts_size = 6u * LINUX_UTSNAME_LEN;
    if (!process_user_buffer_is_valid((const void *)(uintptr_t)uts_ptr,
                                      uts_size)) {
        return LINUX_EFAULT;
    }
    char uts[6 * LINUX_UTSNAME_LEN];
    memset(uts, 0, sizeof(uts));
    memcpy(uts + 0u, "Linux", 5u);
    memcpy(uts + 1u * LINUX_UTSNAME_LEN, "implus", 6u);
    memcpy(uts + 2u * LINUX_UTSNAME_LEN, "6.1.0-implus", 12u);
    memcpy(uts + 3u * LINUX_UTSNAME_LEN, "#1 SMP ImplusOS", 15u);
    memcpy(uts + 4u * LINUX_UTSNAME_LEN, "x86_64", 6u);
    if (copy_to_user((void *)(uintptr_t)uts_ptr, uts, sizeof(uts)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

static int64_t linux_getcpu(uint64_t cpu_ptr, uint64_t node_ptr,
                            uint64_t tcache_ptr)
{
    (void)tcache_ptr;
    uint32_t zero = 0;
    if (cpu_ptr != 0u &&
        copy_to_user((void *)(uintptr_t)cpu_ptr, &zero, sizeof(zero)) != 0u) {
        return LINUX_EFAULT;
    }
    if (node_ptr != 0u &&
        copy_to_user((void *)(uintptr_t)node_ptr, &zero, sizeof(zero)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

static int64_t linux_clone(uint64_t saved_rsp, uint64_t flags, uint64_t stack,
                           uint64_t parent_tid, uint64_t child_tid,
                           uint64_t tls)
{
    (void)tls;
    if (stack == 0u) {
        return LINUX_EINVAL;
    }
    uint64_t *frame = (uint64_t *)(uintptr_t)saved_rsp;
    uint64_t return_rip = frame[SYSCALL_FRAME_RCX];
    if (return_rip < 0x1000u) {
        return LINUX_EFAULT;
    }
    int32_t tid = process_create_thread(return_rip, flags, stack,
                                        parent_tid, child_tid);
    if (tid < 0) {
        return -1;
    }
    process_set_thread_user_rsp(tid, stack);
    if ((flags & LINUX_CLONE_PARENT_SETTID) != 0u && parent_tid != 0u) {
        int32_t tid32 = tid;
        if (copy_to_user((void *)(uintptr_t)parent_tid,
                         &tid32, sizeof(tid32)) != 0u) {
            (void)process_terminate(tid);
            return LINUX_EFAULT;
        }
    }
    if ((flags & LINUX_CLONE_CHILD_SETTID) != 0u && child_tid != 0u) {
        int32_t tid32 = tid;
        if (copy_to_user((void *)(uintptr_t)child_tid,
                         &tid32, sizeof(tid32)) != 0u) {
            (void)process_terminate(tid);
            return LINUX_EFAULT;
        }
    }
    if ((flags & LINUX_CLONE_CHILD_CLEARTID) != 0u && child_tid != 0u) {
        (void)process_set_clear_child_tid(child_tid);
    }
    return (int64_t)tid;
}

static int64_t linux_read(uint64_t fd, uint64_t buf, uint64_t count)
{
    if (count == 0u) {
        return 0;
    }
    if (count > LINUX_MAX_IO_BYTES) {
        count = LINUX_MAX_IO_BYTES;
    }
    if (!process_user_buffer_is_valid((const void *)(uintptr_t)buf, count)) {
        return LINUX_EFAULT;
    }
    return (int64_t)syscall_file_read((int32_t)fd,
                                      (uint8_t *)(uintptr_t)buf, count);
}

static int64_t linux_resolve_path(char *path, uint64_t capacity)
{
    if (path[0] == '/') {
        return 0;
    }
    char cwd[256];
    if (process_get_current_cwd(cwd, sizeof(cwd)) != 0) {
        cwd[0] = '/';
        cwd[1] = '\0';
    }
    uint64_t cwd_len = strlen(cwd);
    while (cwd_len > 1u && cwd[cwd_len - 1u] == '/') {
        cwd[cwd_len - 1u] = '\0';
        --cwd_len;
    }
    uint64_t path_len = strlen(path);
    if (cwd_len + path_len + 2u > capacity) {
        return LINUX_ENAMETOOLONG;
    }
    memmove(path + cwd_len + 1u, path, path_len + 1u);
    memcpy(path, cwd, cwd_len);
    path[cwd_len] = '/';
    return 0;
}

static int64_t linux_open_path(uint64_t path_ptr, uint64_t flags)
{
    char path[256];
    int64_t rc = linux_copy_cstring(path, sizeof(path),
                                    (const char *)(uintptr_t)path_ptr);
    if (rc < 0) {
        return rc;
    }
    rc = linux_resolve_path(path, sizeof(path));
    if (rc < 0) {
        return rc;
    }
    int64_t result;
    if ((flags & LINUX_O_DIRECTORY) != 0u) {
        result = (int64_t)syscall_file_register_dir(path);
    } else {
        result = (int64_t)syscall_file_open(path, flags);
        if (result == LINUX_ENOENT && (flags & LINUX_O_CREAT) != 0u) {
            result = (int64_t)syscall_file_creat(path);
        }
    }
    return result;
}

static int64_t linux_execve(uint64_t path_ptr, uint64_t argv_ptr,
                            uint64_t envp_ptr)
{
    return (int64_t)process_execve(
        (const char *)(uintptr_t)path_ptr,
        (const char *const *)(uintptr_t)argv_ptr,
        (const char *const *)(uintptr_t)envp_ptr);
}

typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    int64_t st_atime_nsec;
    int64_t st_mtime;
    int64_t st_mtime_nsec;
    int64_t st_ctime;
    int64_t st_ctime_nsec;
    int64_t __unused[3];
} linux_stat64_t;

static void linux_stat_fill_common(linux_stat64_t *st, uint64_t size)
{
    st->st_dev = 0x8200u;
    st->st_ino = 1;
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_size = (int64_t)size;
    st->st_blksize = 512;
    st->st_blocks = (size + 511u) / 512u;
    st->st_atime = linux_realtime_seconds();
    st->st_mtime = st->st_atime;
    st->st_ctime = st->st_atime;
}

static int64_t linux_stat_path(const char *path, uint64_t statbuf_ptr)
{
    if (statbuf_ptr == 0u ||
        !process_user_buffer_is_valid((const void *)(uintptr_t)statbuf_ptr,
                                      sizeof(linux_stat64_t))) {
        return LINUX_EFAULT;
    }
    linux_stat64_t st;
    memset(&st, 0, sizeof(st));
    vfs_file_t vf;
    if (vfs_find_file(path, &vf)) {
        linux_stat_fill_common(&st, vf.size);
        st.st_mode = LINUX_S_IFREG | 0x1A4u;
    } else {
        int32_t dir_handle = vfs_opendir(path);
        if (dir_handle < 0) {
            return LINUX_ENOENT;
        }
        (void)vfs_closedir(dir_handle);
        linux_stat_fill_common(&st, 0);
        st.st_mode = LINUX_S_IFDIR | 0x1EDu;
    }
    if (copy_to_user((void *)(uintptr_t)statbuf_ptr, &st, sizeof(st)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

static int64_t linux_stat_fd(int32_t fd, uint64_t statbuf_ptr)
{
    if (statbuf_ptr == 0u ||
        !process_user_buffer_is_valid((const void *)(uintptr_t)statbuf_ptr,
                                      sizeof(linux_stat64_t))) {
        return LINUX_EFAULT;
    }
    linux_stat64_t st;
    memset(&st, 0, sizeof(st));

    vfs_file_t vf;
    uint32_t writable = 0;
    if (syscall_file_get_file_info(fd, &vf, &writable) == 0) {
        linux_stat_fill_common(&st, vf.size);
        st.st_mode = LINUX_S_IFREG | (writable != 0u ? 0x1A4u : 0x124u);
    } else {
        syscall_socket_info_t info;
        if (syscall_socket_get_info(fd, &info) == 0) {
            linux_stat_fill_common(&st, 0);
            st.st_mode = LINUX_S_IFREG | 0x1A4u;
        } else {
            return LINUX_EBADF;
        }
    }
    if (copy_to_user((void *)(uintptr_t)statbuf_ptr, &st, sizeof(st)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

typedef struct {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} linux_dirent64_t;

static int64_t linux_getdents64(int32_t fd, uint64_t buf_ptr, uint64_t count)
{
    if (buf_ptr == 0u || count == 0u ||
        !process_user_buffer_is_valid((const void *)(uintptr_t)buf_ptr,
                                      count)) {
        return LINUX_EFAULT;
    }
    uint64_t written = 0;
    for (;;) {
        vfs_dirent_t entry;
        int32_t rc = syscall_file_get_dir_dirent(fd, &entry);
        if (rc <= 0) {
            break;
        }
        uint64_t name_len = 0;
        while (name_len < sizeof(entry.name) && entry.name[name_len] != '\0') {
            ++name_len;
        }
        uint16_t reclen =
            (uint16_t)((offsetof(linux_dirent64_t, d_name) + name_len + 1u + 7u) &
                       ~7ULL);
        if (written + reclen > count) {
            break;
        }
        linux_dirent64_t d;
        memset(&d, 0, sizeof(d));
        d.d_ino = 1;
        d.d_reclen = reclen;
        d.d_type = entry.is_directory ? 4u : 8u;
        memcpy(d.d_name, entry.name, name_len);
        d.d_name[name_len] = '\0';
        d.d_off = (int64_t)written + reclen;
        if (copy_to_user((uint8_t *)(uintptr_t)buf_ptr + written,
                         &d, reclen) != 0u) {
            return LINUX_EFAULT;
        }
        written += reclen;
    }
    return (int64_t)written;
}

static int64_t linux_prctl(uint64_t option, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5)
{
    (void)arg3;
    (void)arg4;
    (void)arg5;
    switch (option) {
        case LINUX_PR_SET_NAME: {
            char name[16];
            if (linux_copy_cstring(name, sizeof(name),
                                   (const char *)(uintptr_t)arg2) < 0) {
                return LINUX_EFAULT;
            }
            return (int64_t)process_set_current_name(name, 15u);
        }
        case LINUX_PR_GET_NAME: {
            char name[16];
            if (process_get_current_name(name, sizeof(name)) < 0) {
                return -1;
            }
            name[15] = '\0';
            if (!process_user_buffer_is_valid((void *)(uintptr_t)arg2, 16u)) {
                return LINUX_EFAULT;
            }
            return copy_to_user((void *)(uintptr_t)arg2,
                                name, 16u) != 0u ? LINUX_EFAULT : 0;
        }
        default:
            return LINUX_ENOTSUP;
    }
}

static int64_t linux_getcwd(uint64_t buf, uint64_t size)
{
    if (buf == 0u || size == 0u) {
        return LINUX_EINVAL;
    }
    if (!process_user_buffer_is_valid((void *)(uintptr_t)buf, size)) {
        return LINUX_EFAULT;
    }
    char cwd[256];
    if (process_get_current_cwd(cwd, sizeof(cwd)) < 0) {
        return -1;
    }
    uint64_t len = strlen(cwd) + 1u;
    if (len > size) {
        return LINUX_ENAMETOOLONG;
    }
    return copy_to_user((void *)(uintptr_t)buf, cwd, len) != 0u ?
        LINUX_EFAULT : (int64_t)len;
}

static int64_t linux_chdir(uint64_t path_ptr)
{
    char path[256];
    int64_t rc = linux_copy_cstring(path, sizeof(path),
                                    (const char *)(uintptr_t)path_ptr);
    if (rc < 0) {
        return rc;
    }
    rc = linux_resolve_path(path, sizeof(path));
    if (rc < 0) {
        return rc;
    }
    vfs_file_t vf;
    if (vfs_find_file(path, &vf)) {
        return LINUX_ENOTDIR;
    }
    int32_t dir_handle = vfs_opendir(path);
    if (dir_handle < 0) {
        return LINUX_ENOENT;
    }
    (void)vfs_closedir(dir_handle);
    return (int64_t)process_set_current_cwd(path);
}

typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
} linux_sockaddr_in_t;

static inline uint16_t linux_be16_to_host(uint16_t value)
{
    return (uint16_t)((value >> 8) | (uint16_t)(value << 8));
}

static inline uint32_t linux_be32_to_host(uint32_t value)
{
    return ((value & 0xFFu) << 24) | ((value & 0xFF00u) << 8) |
           ((value >> 8) & 0xFF00u) | ((value >> 24) & 0xFFu);
}

static int64_t linux_copy_sockaddr_in(uint64_t addr_ptr, uint32_t *ip_out,
                                      uint16_t *port_out)
{
    if (addr_ptr == 0u) {
        return LINUX_EFAULT;
    }
    linux_sockaddr_in_t addr;
    if (copy_from_user(&addr, (const void *)(uintptr_t)addr_ptr,
                       sizeof(addr)) != 0u) {
        return LINUX_EFAULT;
    }
    if (addr.sin_family != LINUX_AF_INET) {
        return LINUX_EAFNOSUPPORT;
    }
    if (ip_out != NULL) *ip_out = linux_be32_to_host(addr.sin_addr);
    if (port_out != NULL) *port_out = linux_be16_to_host(addr.sin_port);
    return 0;
}

static int64_t linux_socket_create(uint64_t domain, uint64_t type,
                                   uint64_t protocol)
{
    (void)protocol;
    if (domain != LINUX_AF_INET) {
        return LINUX_EAFNOSUPPORT;
    }
    if ((type & 0xFu) != LINUX_SOCK_STREAM) {
        return LINUX_EPROTONOSUPPORT;
    }
    return (int64_t)syscall_socket_create(LINUX_SOCK_STREAM);
}

static int64_t linux_socket_bind(uint64_t fd, uint64_t addr_ptr,
                                 uint64_t addr_len)
{
    (void)addr_len;
    uint32_t ip;
    uint16_t port;
    int64_t rc = linux_copy_sockaddr_in(addr_ptr, &ip, &port);
    if (rc < 0) {
        return rc;
    }
    (void)ip;
    return (int64_t)syscall_socket_bind((int32_t)fd, port);
}

static int64_t linux_socket_connect(uint64_t fd, uint64_t addr_ptr,
                                    uint64_t addr_len)
{
    (void)addr_len;
    uint32_t ip;
    uint16_t port;
    int64_t rc = linux_copy_sockaddr_in(addr_ptr, &ip, &port);
    if (rc < 0) {
        return rc;
    }
    return (int64_t)syscall_socket_connect((int32_t)fd, ip, port);
}

static int64_t linux_socket_accept(uint64_t fd, uint64_t addr_ptr,
                                   uint64_t addr_len_ptr)
{
    if (addr_ptr != 0u && addr_len_ptr != 0u) {
        int32_t addr_len = 0;
        if (copy_from_user(&addr_len, (const void *)(uintptr_t)addr_len_ptr,
                           sizeof(addr_len)) != 0u) {
            return LINUX_EFAULT;
        }
        if (addr_len < (int32_t)sizeof(linux_sockaddr_in_t)) {
            return LINUX_EINVAL;
        }
        int32_t accepted = syscall_socket_accept((int32_t)fd);
        if (accepted < 0) {
            return (int64_t)accepted;
        }
        syscall_socket_info_t info;
        if (syscall_socket_get_info(accepted, &info) != 0) {
            return LINUX_EBADF;
        }
        linux_sockaddr_in_t addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = LINUX_AF_INET;
        addr.sin_port = linux_be16_to_host(info.remote_port);
        addr.sin_addr = info.remote_ip;
        if (copy_to_user((void *)(uintptr_t)addr_ptr, &addr,
                         sizeof(addr)) != 0u) {
            return LINUX_EFAULT;
        }
        if (copy_to_user((void *)(uintptr_t)addr_len_ptr,
                         &addr_len, sizeof(addr_len)) != 0u) {
            return LINUX_EFAULT;
        }
        return accepted;
    }
    return (int64_t)syscall_socket_accept((int32_t)fd);
}

static int64_t linux_socket_sendto(uint64_t fd, uint64_t buf, uint64_t len,
                                   uint64_t flags, uint64_t addr_ptr,
                                   uint64_t addr_len)
{
    (void)flags;
    if (addr_ptr != 0u) {
        uint32_t ip;
        uint16_t port;
        int64_t rc = linux_copy_sockaddr_in(addr_ptr, &ip, &port);
        if (rc < 0) {
            return rc;
        }
        if (port != 0u && ip != 0u) {
            int64_t rc2 = linux_socket_connect(fd, addr_ptr, addr_len);
            if (rc2 < 0) {
                return rc2;
            }
        }
    }
    if (len > 65535u) {
        len = 65535u;
    }
    if (len != 0u &&
        !process_user_buffer_is_valid((const void *)(uintptr_t)buf, len)) {
        return LINUX_EFAULT;
    }
    return (int64_t)syscall_socket_send((int32_t)fd,
                                        (const void *)(uintptr_t)buf,
                                        (uint16_t)len);
}

static int64_t linux_socket_recvfrom(uint64_t fd, uint64_t buf, uint64_t len,
                                     uint64_t flags, uint64_t addr_ptr,
                                     uint64_t addr_len_ptr)
{
    (void)flags;
    (void)addr_ptr;
    (void)addr_len_ptr;
    if (len > 65535u) {
        len = 65535u;
    }
    if (len != 0u &&
        !process_user_buffer_is_valid((const void *)(uintptr_t)buf, len)) {
        return LINUX_EFAULT;
    }
    return (int64_t)syscall_socket_recv((int32_t)fd, (void *)(uintptr_t)buf,
                                        (uint16_t)len);
}

static int64_t linux_socket_setsockopt(uint64_t fd, uint64_t level,
                                       uint64_t option, uint64_t value_ptr,
                                       uint64_t value_len)
{
    if (value_len != sizeof(int32_t) || value_ptr == 0u) {
        return LINUX_EINVAL;
    }
    int32_t value = 0;
    if (copy_from_user(&value, (const void *)(uintptr_t)value_ptr,
                       sizeof(value)) != 0u) {
        return LINUX_EFAULT;
    }
    if (level != LINUX_SOL_SOCKET) {
        return LINUX_ENOPROTOOPT;
    }
    int32_t mapped_option = 0;
    switch (option) {
        case LINUX_SO_REUSEADDR: mapped_option = 1; break;
        case LINUX_SO_KEEPALIVE: mapped_option = 2; break;
        default: return LINUX_ENOPROTOOPT;
    }
    return (int64_t)syscall_socket_set_option((int32_t)fd, (int32_t)level,
                                              mapped_option, value);
}

static int64_t linux_socket_getsockopt(uint64_t fd, uint64_t level,
                                       uint64_t option, uint64_t value_ptr,
                                       uint64_t value_len_ptr)
{
    if (value_ptr == 0u || value_len_ptr == 0u) {
        return LINUX_EFAULT;
    }
    int32_t value_len = 0;
    if (copy_from_user(&value_len, (const void *)(uintptr_t)value_len_ptr,
                       sizeof(value_len)) != 0u) {
        return LINUX_EFAULT;
    }
    if (value_len < (int32_t)sizeof(int32_t)) {
        return LINUX_EINVAL;
    }
    if (level != LINUX_SOL_SOCKET) {
        return LINUX_ENOPROTOOPT;
    }
    int32_t mapped_option = 0;
    switch (option) {
        case LINUX_SO_REUSEADDR: mapped_option = 1; break;
        case LINUX_SO_KEEPALIVE: mapped_option = 2; break;
        default: return LINUX_ENOPROTOOPT;
    }
    int32_t value = 0;
    int32_t rc = syscall_socket_get_option((int32_t)fd, (int32_t)level,
                                           mapped_option, &value);
    if (rc < 0) {
        return (int64_t)rc;
    }
    if (copy_to_user((void *)(uintptr_t)value_ptr, &value,
                     sizeof(value)) != 0u) {
        return LINUX_EFAULT;
    }
    value_len = (int32_t)sizeof(value);
    if (copy_to_user((void *)(uintptr_t)value_len_ptr, &value_len,
                     sizeof(value_len)) != 0u) {
        return LINUX_EFAULT;
    }
    return 0;
}

uint64_t linux_syscall_dispatch(uint64_t saved_rsp,
                                uint64_t num,
                                uint64_t arg1,
                                uint64_t arg2,
                                uint64_t arg3,
                                uint64_t arg4,
                                uint64_t arg5,
                                uint64_t arg6)
{
    int64_t result = LINUX_ENOSYS;
    int request_switch = 0;

    switch (num) {
        case LINUX_SYS_READ:
            result = linux_read(arg1, arg2, arg3);
            break;

        case LINUX_SYS_WRITE:
            result = write((int32_t)arg1, (const void *)(uintptr_t)arg2,
                           arg3);
            break;

        case LINUX_SYS_OPEN:
            result = linux_open_path(arg1, arg2);
            break;

        case LINUX_SYS_CLOSE:
            result = (int64_t)syscall_file_close((int32_t)arg1);
            break;

        case LINUX_SYS_STAT:
        case LINUX_SYS_LSTAT: {
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            result = linux_stat_path(path, arg2);
            break;
        }

        case LINUX_SYS_FSTAT:
            result = linux_stat_fd((int32_t)arg1, arg2);
            break;

        case LINUX_SYS_NEWFSTATAT: {
            if ((int64_t)arg1 != LINUX_AT_FDCWD) {
                result = LINUX_ENOTSUP;
                break;
            }
            (void)arg4;
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg2);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            result = linux_stat_path(path, arg3);
            break;
        }

        case LINUX_SYS_LSEEK:
            result = (int64_t)syscall_file_seek((int32_t)arg1,
                                                (int64_t)arg2,
                                                (int32_t)arg3);
            break;

        case LINUX_SYS_MMAP:
            result = linux_mmap(arg1, arg2, arg3, arg4, arg5, arg6);
            break;

        case LINUX_SYS_MPROTECT:
            result = syscall_vm_mprotect(arg1, arg2, arg3);
            break;

        case LINUX_SYS_MUNMAP:
            result = (int64_t)process_user_munmap((void *)(uintptr_t)arg1,
                                                  arg2);
            break;

        case LINUX_SYS_BRK:
            result = linux_brk(arg1);
            break;

        case LINUX_SYS_RT_SIGACTION:
            result = linux_rt_sigaction(arg1, arg2, arg3, arg4);
            break;

        case LINUX_SYS_RT_SIGPROCMASK:
            result = linux_rt_sigprocmask(arg1, arg2, arg3, arg4);
            break;

        case LINUX_SYS_IOCTL:
            result = syscall_ioctl_ex(arg1, arg2, arg3);
            break;

        case LINUX_SYS_READV:
            result = syscall_readv((int32_t)arg1, arg2, (int32_t)arg3);
            break;

        case LINUX_SYS_WRITEV:
            result = syscall_writev((int32_t)arg1, arg2, (int32_t)arg3);
            break;

        case LINUX_SYS_ACCESS: {
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            result = syscall_access(path, (int32_t)arg2);
            break;
        }

        case LINUX_SYS_PIPE: {
            if (!process_user_buffer_is_valid((const void *)(uintptr_t)arg1,
                                              sizeof(int32_t) * 2u)) {
                result = LINUX_EFAULT;
                break;
            }
            int32_t fds[2];
            int32_t rc = syscall_file_pipe(fds);
            if (rc >= 0 &&
                copy_to_user((void *)(uintptr_t)arg1, fds, sizeof(fds)) != 0u) {
                result = LINUX_EFAULT;
                break;
            }
            result = rc;
            break;
        }

        case LINUX_SYS_SCHED_YIELD:
            result = 0;
            request_switch = 1;
            break;

        case LINUX_SYS_DUP:
            result = (int64_t)syscall_file_dup((int32_t)arg1);
            break;

        case LINUX_SYS_DUP2:
            result = (int64_t)syscall_file_dup2((int32_t)arg1, (int32_t)arg2);
            break;

        case LINUX_SYS_NANOSLEEP: {
            struct {
                int64_t sec;
                int64_t nsec;
            } req;
            if (copy_from_user(&req, (const void *)(uintptr_t)arg1,
                               sizeof(req)) != 0u) {
                result = LINUX_EFAULT;
                break;
            }
            if (req.sec < 0 || req.nsec < 0 || req.nsec >= 1000000000LL) {
                result = LINUX_EINVAL;
                break;
            }
            uint64_t ms = (uint64_t)req.sec * 1000u +
                          ((uint64_t)req.nsec + 999999u) / 1000000u;
            if (ms > 0u && process_sleep_current_ms(ms) == 0) {
                request_switch = 1;
            }
            result = 0;
            break;
        }

        case LINUX_SYS_GETPID:
            result = (int64_t)process_get_current_pid();
            break;

        case LINUX_SYS_CLONE:
            result = linux_clone(saved_rsp, arg1, arg2, arg3, arg4, arg5);
            break;

        case LINUX_SYS_FORK:
        case LINUX_SYS_VFORK: {
            int32_t child_pid = process_fork();
            if (child_pid > 0) {
                request_switch = 1;
            }
            result = (int64_t)child_pid;
            break;
        }

        case LINUX_SYS_EXECVE:
            result = linux_execve(arg1, arg2, arg3);
            request_switch = 1;
            break;

        case LINUX_SYS_EXIT:
        case LINUX_SYS_EXIT_GROUP:
            process_exit_current_with_status((int32_t)arg1 & 0xFF);
            result = 0;
            request_switch = 1;
            break;

        case LINUX_SYS_WAIT4:
            result = linux_wait4(arg1, arg2, arg3, arg4);
            break;

        case LINUX_SYS_KILL:
            result = (int64_t)process_signal_deliver((int32_t)arg1,
                                                     (int32_t)arg2);
            break;

        case LINUX_SYS_UNAME:
            result = linux_uname(arg1);
            break;

        case LINUX_SYS_FCNTL:
            result = syscall_fcntl_ex((int32_t)arg1, (int32_t)arg2, arg3);
            break;

        case LINUX_SYS_FTRUNCATE:
            result = syscall_ftruncate((int32_t)arg1, (int64_t)arg2);
            break;

        case LINUX_SYS_GETCWD:
            result = linux_getcwd(arg1, arg2);
            break;

        case LINUX_SYS_CHDIR:
            result = linux_chdir(arg1);
            break;

        case LINUX_SYS_RENAME: {
            char old_path[256];
            char new_path[256];
            int64_t rc = linux_copy_cstring(old_path, sizeof(old_path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_copy_cstring(new_path, sizeof(new_path),
                                    (const char *)(uintptr_t)arg2);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(old_path, sizeof(old_path));
            if (rc >= 0) {
                rc = linux_resolve_path(new_path, sizeof(new_path));
            }
            if (rc < 0) {
                result = rc;
                break;
            }
            result = (int64_t)syscall_file_rename(old_path, new_path);
            break;
        }

        case LINUX_SYS_MKDIR: {
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            result = (int64_t)syscall_file_mkdir(path);
            break;
        }

        case LINUX_SYS_RMDIR: {
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            vfs_file_t vf;
            if (vfs_find_file(path, &vf)) {
                result = LINUX_ENOTDIR;
                break;
            }
            result = (int64_t)syscall_file_unlink(path);
            break;
        }

        case LINUX_SYS_CREAT: {
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            result = (int64_t)syscall_file_creat(path);
            break;
        }

        case LINUX_SYS_UNLINK: {
            char path[256];
            int64_t rc = linux_copy_cstring(path, sizeof(path),
                                            (const char *)(uintptr_t)arg1);
            if (rc < 0) {
                result = rc;
                break;
            }
            rc = linux_resolve_path(path, sizeof(path));
            if (rc < 0) {
                result = rc;
                break;
            }
            result = (int64_t)syscall_file_unlink(path);
            break;
        }

        case LINUX_SYS_GETTIMEOFDAY:
            result = linux_gettimeofday(arg1);
            break;

        case LINUX_SYS_GETRLIMIT:
            result = syscall_prlimit64(0u, arg2, 0u, arg1);
            break;

        case LINUX_SYS_GETUID:
        case LINUX_SYS_GETGID:
        case LINUX_SYS_GETEUID:
        case LINUX_SYS_GETEGID:
            result = 0;
            break;

        case LINUX_SYS_GETPPID:
            result = (int64_t)process_getppid();
            break;

        case LINUX_SYS_PRCTL:
            result = linux_prctl(arg1, arg2, arg3, arg4, arg5);
            break;

        case LINUX_SYS_ARCH_PRCTL:
            result = syscall_arch_prctl(arg1, arg2);
            break;

        case LINUX_SYS_SETRLIMIT:
            result = 0;
            break;

        case LINUX_SYS_GETTID:
            result = syscall_gettid();
            break;

        case LINUX_SYS_TKILL:
            result = (int64_t)process_signal_deliver((int32_t)arg1,
                                                     (int32_t)arg2);
            break;

        case LINUX_SYS_TIME:
            result = linux_realtime_seconds();
            break;

        case LINUX_SYS_FUTEX: {
            result = syscall_futex(arg1, arg2, arg3, arg4, arg5, arg6);
            uint64_t command = arg2 & 0x7fULL;
            if (result == 0 && (command == 0u || command == 9u)) {
                request_switch = 1;
            }
            break;
        }

        case LINUX_SYS_GETDENTS64:
            result = linux_getdents64((int32_t)arg1, arg2, arg3);
            break;

        case LINUX_SYS_SET_TID_ADDRESS:
            result = syscall_set_tid_address(arg1);
            break;

        case LINUX_SYS_CLOCK_GETTIME:
            result = syscall_clock_gettime((int32_t)arg1, arg2);
            break;

        case LINUX_SYS_CLOCK_GETRES:
            result = syscall_clock_getres((int32_t)arg1, arg2);
            break;

        case LINUX_SYS_EPOLL_WAIT:
            result = linux_epoll_wait(arg1, arg2, arg3, arg4);
            break;

        case LINUX_SYS_EPOLL_CTL:
            result = linux_epoll_ctl(arg1, arg2, arg3, arg4);
            break;

        case LINUX_SYS_TGKILL:
            result = (int64_t)process_signal_deliver((int32_t)arg2,
                                                     (int32_t)arg3);
            break;

        case LINUX_SYS_OPENAT: {
            if ((int64_t)arg1 != -100) {
                result = LINUX_ENOTSUP;
                break;
            }
            result = linux_open_path(arg2, arg3);
            break;
        }

        case LINUX_SYS_SET_ROBUST_LIST:
            result = (int64_t)process_set_robust_list(arg1, arg2);
            break;

        case LINUX_SYS_TIMERFD_CREATE: {
            (void)arg1;
            (void)arg2;
            result = (int64_t)syscall_file_create_timerfd();
            break;
        }

        case LINUX_SYS_TIMERFD_SETTIME: {
            struct {
                int64_t sec;
                int64_t nsec;
            } it_value;
            struct {
                int64_t sec;
                int64_t nsec;
            } it_interval;
            if (copy_from_user(&it_value, (const void *)(uintptr_t)arg3,
                               sizeof(it_value)) != 0u ||
                copy_from_user(&it_interval, (const void *)(uintptr_t)arg4,
                               sizeof(it_interval)) != 0u) {
                result = LINUX_EFAULT;
                break;
            }
            if (it_value.nsec < 0 || it_value.nsec >= 1000000000LL ||
                it_interval.nsec < 0 || it_interval.nsec >= 1000000000LL) {
                result = LINUX_EINVAL;
                break;
            }
            result = (int64_t)syscall_file_timerfd_settime(
                (int32_t)arg1,
                (uint64_t)it_value.sec, (uint64_t)it_value.nsec,
                (uint64_t)it_interval.sec, (uint64_t)it_interval.nsec);
            break;
        }

        case LINUX_SYS_TIMERFD_GETTIME: {
            uint64_t value_sec = 0;
            uint64_t value_nsec = 0;
            uint64_t interval_sec = 0;
            uint64_t interval_nsec = 0;
            result = (int64_t)syscall_file_timerfd_gettime(
                (int32_t)arg1, &value_sec, &value_nsec,
                &interval_sec, &interval_nsec);
            if (result == 0) {
                struct {
                    int64_t sec;
                    int64_t nsec;
                } value;
                struct {
                    int64_t sec;
                    int64_t nsec;
                } interval;
                value.sec = (int64_t)value_sec;
                value.nsec = (int64_t)value_nsec;
                interval.sec = (int64_t)interval_sec;
                interval.nsec = (int64_t)interval_nsec;
                if (copy_to_user((void *)(uintptr_t)arg2, &value,
                                 sizeof(value)) != 0u ||
                    copy_to_user((void *)(uintptr_t)arg3, &interval,
                                 sizeof(interval)) != 0u) {
                    result = LINUX_EFAULT;
                }
            }
            break;
        }

        case LINUX_SYS_SIGNALFD4: {
            if (arg3 != sizeof(uint64_t)) {
                result = LINUX_EINVAL;
                break;
            }
            uint64_t mask = 0;
            if (copy_from_user(&mask, (const void *)(uintptr_t)arg2,
                               sizeof(mask)) != 0u) {
                result = LINUX_EFAULT;
                break;
            }
            result = (int64_t)syscall_file_create_signalfd(mask);
            break;
        }

        case LINUX_SYS_MEMFD_CREATE:
            result = (int64_t)syscall_file_create_memfd(
                (const char *)(uintptr_t)arg1);
            break;

        case LINUX_SYS_RSEQ:
            result = LINUX_ENOTSUP;
            break;

        case LINUX_SYS_EVENTFD:
            result = (int64_t)syscall_eventfd(arg1, 0u);
            break;

        case LINUX_SYS_EVENTFD2:
            result = (int64_t)syscall_eventfd(arg1, arg2);
            break;

        case LINUX_SYS_EPOLL_CREATE1:
            result = (int64_t)syscall_epoll_create(arg1);
            break;

        case LINUX_SYS_PRLIMIT64:
            result = syscall_prlimit64(arg1, arg2, arg3, arg4);
            break;

        case LINUX_SYS_GETCPU:
            result = linux_getcpu(arg1, arg2, arg3);
            break;

        case LINUX_SYS_GETRANDOM:
            result = syscall_getrandom(arg1, arg2, arg3);
            break;

        case LINUX_SYS_SOCKET:
            result = linux_socket_create(arg1, arg2, arg3);
            break;

        case LINUX_SYS_BIND:
            result = linux_socket_bind(arg1, arg2, arg3);
            break;

        case LINUX_SYS_CONNECT:
            result = linux_socket_connect(arg1, arg2, arg3);
            break;

        case LINUX_SYS_LISTEN:
            result = (int64_t)syscall_socket_listen((int32_t)arg1);
            break;

        case LINUX_SYS_ACCEPT:
            result = linux_socket_accept(arg1, arg2, arg3);
            break;

        case LINUX_SYS_SENDTO:
            result = linux_socket_sendto(arg1, arg2, arg3, arg4, arg5, arg6);
            break;

        case LINUX_SYS_RECVFROM:
            result = linux_socket_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6);
            break;

        case LINUX_SYS_SHUTDOWN:
            result = (int64_t)syscall_socket_shutdown((int32_t)arg1,
                                                      (int32_t)arg2);
            break;

        case LINUX_SYS_SETSOCKOPT:
            result = linux_socket_setsockopt(arg1, arg2, arg3, arg4, arg5);
            break;

        case LINUX_SYS_GETSOCKOPT:
            result = linux_socket_getsockopt(arg1, arg2, arg3, arg4, arg5);
            break;

        default:
            result = LINUX_ENOSYS;
            break;
    }

    linux_syscall_result(saved_rsp, result);
    return (uint64_t)(uint32_t)request_switch;
}
