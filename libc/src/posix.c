#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscalls.h>

typedef struct {
    uint32_t size;
    uint8_t  is_dir;
    uint8_t  exists;
} os_file_stat_t;

typedef struct {
    char     name[260];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t  attributes;
} os_file_dirent_t;

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} rtc_time_t;

extern int32_t file_open(const char* path, uint64_t flags);
extern int32_t file_creat(const char* path);
extern int64_t file_read(int32_t fd, void* buffer, uint64_t len);
extern int64_t file_write(int32_t fd, const void* buffer, uint64_t len);
extern int64_t file_seek(int32_t fd, int64_t offset, int32_t whence);
extern int32_t file_close(int32_t fd);
extern int32_t file_mkdir(const char* path);
extern int32_t file_opendir(const char* path);
extern int32_t file_readdir(int32_t dir_handle, os_file_dirent_t* out_entry);
extern int32_t file_closedir(int32_t dir_handle);
extern int32_t file_unlink(const char* path);
extern int32_t file_rename(const char* old_path, const char* new_path);
extern int32_t file_stat(const char* path, os_file_stat_t* stat_out);
extern int32_t file_pipe(int32_t fds[2]);
extern int32_t file_dup(int32_t oldfd);
extern int32_t file_dup2(int32_t oldfd, int32_t newfd);
extern int32_t socket_create(int32_t type);
extern int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port);
extern int32_t socket_bind(int32_t sockfd, uint16_t port);
extern int32_t socket_listen(int32_t sockfd);
extern int32_t socket_listen_with_backlog(int32_t sockfd, int32_t backlog);
extern int32_t socket_accept(int32_t sockfd);
extern int32_t socket_send(int32_t sockfd, const void* data, uint32_t len);
extern int32_t socket_recv(int32_t sockfd, void* buf, uint32_t buf_len);
extern int32_t socket_close(int32_t sockfd);
extern int32_t socket_set_option(int32_t sockfd, int32_t level,
                                 int32_t option, int32_t value);
extern int32_t socket_get_option(int32_t sockfd, int32_t level,
                                 int32_t option, int32_t *value_out);
extern int32_t socket_shutdown(int32_t sockfd, int32_t how);

typedef struct {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t state;
    int32_t error;
} socket_info_t;

extern int32_t socket_get_info(int32_t sockfd, socket_info_t *info_out);
extern int32_t process_waitpid(int32_t pid, int32_t* status_out, int32_t options);
extern int32_t process_get_current_pid(void);
extern int32_t process_getppid(void);
extern void process_exit(int32_t status);
extern void sleep_ms(uint64_t milliseconds);
extern uint64_t get_uptime_ms(void);
extern int32_t sys_get_rtc_time(rtc_time_t* time);
extern sighandler_t os_signal(int32_t signum, sighandler_t handler);

#define POSIX_MAX_TRACKED_FDS 1024
#define POSIX_FD_FILE   1
#define POSIX_FD_PIPE   2
#define POSIX_FD_SOCKET 3

typedef struct {
    int valid;
    int type;
    int status_flags;
    int fd_flags;
} posix_fd_state_t;

static posix_fd_state_t g_fd_state[POSIX_MAX_TRACKED_FDS];

static posix_fd_state_t* posix_fd_state(int fd)
{
    if (fd < 0 || fd >= POSIX_MAX_TRACKED_FDS) {
        return NULL;
    }
    return &g_fd_state[fd];
}

static void posix_fd_mark_open(int fd, int status_flags)
{
    posix_fd_state_t* state = posix_fd_state(fd);
    if (!state) {
        return;
    }
    state->valid = 1;
    state->type = POSIX_FD_FILE;
    state->status_flags = status_flags;
    state->fd_flags = 0;
}

static void posix_fd_mark_closed(int fd)
{
    posix_fd_state_t* state = posix_fd_state(fd);
    if (!state) {
        return;
    }
    state->valid = 0;
    state->type = 0;
    state->status_flags = 0;
    state->fd_flags = 0;
}

static uint16_t byte_swap16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t byte_swap32(uint32_t value)
{
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static void timeval_to_ms(const struct timeval* tv, uint64_t* out_ms)
{
    if (!out_ms) {
        return;
    }
    if (!tv) {
        *out_ms = 0;
        return;
    }
    *out_ms = (uint64_t)tv->tv_sec * 1000ULL;
    if (tv->tv_usec > 0) {
        *out_ms += (uint64_t)(tv->tv_usec / 1000);
    }
}

static int64_t date_to_epoch_secs(int year, int mon, int day,
                                  int hour, int min, int sec);

extern uint64_t syscall0(uint64_t number);
extern uint64_t syscall1(uint64_t number, uint64_t arg1);
extern uint64_t syscall2(uint64_t number, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t number, uint64_t arg1,
                         uint64_t arg2, uint64_t arg3);
extern uint64_t syscall4(uint64_t number, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4);

int open(const char* path, int flags, ...)
{
    int writable = (flags & (O_WRONLY | O_RDWR)) != 0;
    int32_t fd;

    if (flags & O_CREAT) {
        fd = file_creat(path);
    } else {
        fd = file_open(path, (uint64_t)(writable ? 1 : 0));
    }

    if (fd < 0) {
        return -1;
    }

    if (flags & O_APPEND) {
        if (file_seek(fd, 0, SEEK_END) < 0) {
            file_close(fd);
            return -1;
        }
    } else if ((flags & O_TRUNC) && writable) {
        file_close(fd);
        file_unlink(path);
        fd = file_creat(path);
    }

    posix_fd_mark_open(fd, flags & (O_APPEND | O_NONBLOCK | O_RDWR | O_WRONLY));
    return fd;
}

int creat(const char* path, int mode)
{
    (void)mode;
    return open(path, O_CREAT | O_WRONLY | O_TRUNC);
}

ssize_t read(int fd, void* buf, size_t count)
{
    posix_fd_state_t* state = posix_fd_state(fd);
    if (state && state->valid && state->type == POSIX_FD_SOCKET)
        return (ssize_t)socket_recv(fd, buf, (uint32_t)count);
    return (ssize_t)file_read(fd, buf, (uint64_t)count);
}

ssize_t write(int fd, const void* buf, size_t count)
{
    posix_fd_state_t* state = posix_fd_state(fd);
    if (state && state->valid && state->type == POSIX_FD_SOCKET)
        return (ssize_t)socket_send(fd, buf, (uint32_t)count);
    return (ssize_t)file_write(fd, buf, (uint64_t)count);
}

int close(int fd)
{
    posix_fd_state_t* state = posix_fd_state(fd);
    int rc = state && state->valid && state->type == POSIX_FD_SOCKET ?
        socket_close(fd) : file_close(fd);
    if (rc == 0) {
        posix_fd_mark_closed(fd);
    }
    return rc;
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)file_seek(fd, offset, whence);
}

int pipe(int pipefd[2])
{
    int rc = file_pipe(pipefd);
    if (rc == 0) {
        posix_fd_mark_open(pipefd[0], O_RDONLY);
        posix_fd_mark_open(pipefd[1], O_WRONLY);
        posix_fd_state(pipefd[0])->type = POSIX_FD_PIPE;
        posix_fd_state(pipefd[1])->type = POSIX_FD_PIPE;
    }
    return rc;
}

int dup(int oldfd)
{
    int newfd = file_dup(oldfd);
    posix_fd_state_t* old_state = posix_fd_state(oldfd);
    if (newfd >= 0 && old_state) {
        posix_fd_mark_open(newfd, old_state->status_flags);
    }
    return newfd;
}

int dup2(int oldfd, int newfd)
{
    int rc = file_dup2(oldfd, newfd);
    posix_fd_state_t* old_state = posix_fd_state(oldfd);
    if (rc >= 0 && old_state) {
        posix_fd_mark_open(newfd, old_state->status_flags);
    }
    return rc;
}

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    posix_fd_state_t* state = posix_fd_state(fd);

    if (!state || !state->valid) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, cmd);
    switch (cmd) {
        case F_GETFL:
            va_end(ap);
            return state->status_flags;
        case F_SETFL:
            state->status_flags = va_arg(ap, int);
            va_end(ap);
            return 0;
        case F_SETFD:
            state->fd_flags = va_arg(ap, int);
            va_end(ap);
            return 0;
        case F_DUPFD_CLOEXEC: {
            int minfd = va_arg(ap, int);
            int newfd;
            va_end(ap);
            (void)minfd;
            newfd = dup(fd);
            if (newfd >= 0) {
                posix_fd_state_t* new_state = posix_fd_state(newfd);
                if (new_state) {
                    new_state->fd_flags |= FD_CLOEXEC;
                }
            }
            return newfd;
        }
        default:
            va_end(ap);
            errno = ENOTSUP;
            return -1;
    }
}

int stat(const char* path, struct stat* st)
{
    os_file_stat_t info;
    if (!st) {
        errno = EINVAL;
        return -1;
    }
    if (file_stat(path, &info) < 0 || !info.exists) {
        return -1;
    }

    memset(st, 0, sizeof(*st));
    st->st_size = (off_t)info.size;
    st->st_mode = info.is_dir ? S_IFDIR : S_IFREG;
    return 0;
}

int fstat(int fd, struct stat* st)
{
    off_t current;
    off_t end;

    if (!st) {
        errno = EINVAL;
        return -1;
    }

    current = lseek(fd, 0, SEEK_CUR);
    if (current < 0) {
        return -1;
    }

    end = lseek(fd, 0, SEEK_END);
    if (end < 0) {
        return -1;
    }

    if (lseek(fd, current, SEEK_SET) < 0) {
        return -1;
    }

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = end;
    return 0;
}

int mkdir(const char* path, mode_t mode)
{
    (void)mode;
    return file_mkdir(path);
}

int unlink(const char* path)
{
    return file_unlink(path);
}

int rename(const char* old_path, const char* new_path)
{
    if (!old_path || !new_path) {
        errno = EINVAL;
        return -1;
    }
    return file_rename(old_path, new_path);
}

DIR* opendir(const char* path)
{
    DIR* dir;
    int handle = file_opendir(path);
    if (handle < 0) {
        return NULL;
    }

    dir = (DIR*)malloc(sizeof(DIR));
    if (!dir) {
        file_closedir(handle);
        errno = ENOMEM;
        return NULL;
    }

    memset(dir, 0, sizeof(*dir));
    dir->handle = handle;
    return dir;
}

struct dirent* readdir(DIR* dirp)
{
    os_file_dirent_t entry;
    if (!dirp) {
        errno = EINVAL;
        return NULL;
    }
    if (file_readdir(dirp->handle, &entry) < 0) {
        return NULL;
    }

    memset(&dirp->entry, 0, sizeof(dirp->entry));
    strncpy(dirp->entry.d_name, entry.name, sizeof(dirp->entry.d_name) - 1);
    dirp->entry.d_type = (entry.attributes & 0x10u) ? DT_DIR : DT_REG;
    return &dirp->entry;
}

int closedir(DIR* dirp)
{
    int rc;
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }
    rc = file_closedir(dirp->handle);
    free(dirp);
    return rc;
}

int gettimeofday(struct timeval* tv, struct timezone* tz)
{
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    if (!tv) {
        return 0;
    }

    rtc_time_t rtc;
    if (sys_get_rtc_time(&rtc) < 0) {
        errno = EIO;
        return -1;
    }
    tv->tv_sec = (time_t)date_to_epoch_secs(
        (int)rtc.year, (int)rtc.month, (int)rtc.day,
        (int)rtc.hour, (int)rtc.minute, (int)rtc.second);
    tv->tv_usec = 0;
    return 0;
}

uint64_t clock_ms(void)
{
    return get_uptime_ms();
}

time_t time(time_t* out)
{
    struct timeval value;
    if (gettimeofday(&value, NULL) < 0) return (time_t)-1;
    time_t now = value.tv_sec;
    if (out) {
        *out = now;
    }
    return now;
}

int nanosleep(const struct timespec* req, struct timespec* rem)
{
    uint64_t ms = 0;
    (void)rem;
    if (!req) {
        errno = EINVAL;
        return -1;
    }
    if (req->tv_sec > 0) {
        ms += (uint64_t)req->tv_sec * 1000ULL;
    }
    if (req->tv_nsec > 0) {
        ms += (uint64_t)(req->tv_nsec / 1000000L);
    }
    sleep_ms(ms);
    return 0;
}

unsigned int sleep(unsigned int seconds)
{
    sleep_ms((uint64_t)seconds * 1000ULL);
    return 0;
}

int usleep(useconds_t usec)
{
    sleep_ms((uint64_t)(usec / 1000));
    return 0;
}

pid_t waitpid(pid_t pid, int* status, int options)
{
    return process_waitpid(pid, status, options);
}

int32_t getpid(void)
{
    return process_get_current_pid();
}

int32_t getppid(void)
{
    return process_getppid();
}

void _exit(int32_t status)
{
    process_exit(status);
}

typedef struct {
    void* address;
    size_t length;
    int32_t backing_fd;
    off_t offset;
    int shared;
    int prot;
} libc_mapping_t;

#define LIBC_MAPPING_MAX 128
static libc_mapping_t g_libc_mappings[LIBC_MAPPING_MAX];

static libc_mapping_t* libc_mapping_find(void* address, size_t length)
{
    for (size_t i = 0; i < LIBC_MAPPING_MAX; ++i) {
        if (g_libc_mappings[i].address == address &&
            g_libc_mappings[i].length == length) {
            return &g_libc_mappings[i];
        }
    }
    return NULL;
}

static libc_mapping_t* libc_mapping_allocate(void)
{
    for (size_t i = 0; i < LIBC_MAPPING_MAX; ++i) {
        if (g_libc_mappings[i].address == NULL) return &g_libc_mappings[i];
    }
    return NULL;
}

static void libc_mapping_discard_pages(void* address, size_t length)
{
    (void)syscall2(SYSCALL_MUNMAP,
                   (uint64_t)(uintptr_t)address, (uint64_t)length);
}

static int libc_mapping_flush(libc_mapping_t* mapping)
{
    if (!mapping || !mapping->shared || mapping->backing_fd < 0) return 0;
    int restore_protection = (mapping->prot & PROT_READ) == 0;
    if (restore_protection) {
        int64_t protect = (int64_t)syscall3(
            SYSCALL_MPROTECT, (uint64_t)(uintptr_t)mapping->address,
            (uint64_t)mapping->length, PROT_READ);
        if (protect < 0) {
            errno = (int)-protect;
            return -1;
        }
    }
    int64_t saved = file_seek(mapping->backing_fd, 0, SEEK_CUR);
    if (saved < 0 ||
        file_seek(mapping->backing_fd, mapping->offset, SEEK_SET) < 0) {
        if (restore_protection) {
            (void)syscall3(SYSCALL_MPROTECT,
                           (uint64_t)(uintptr_t)mapping->address,
                           (uint64_t)mapping->length,
                           (uint64_t)(unsigned int)mapping->prot);
        }
        errno = EIO;
        return -1;
    }
    size_t total = 0;
    while (total < mapping->length) {
        int64_t written = file_write(
            mapping->backing_fd, (const uint8_t*)mapping->address + total,
            (uint64_t)(mapping->length - total));
        if (written <= 0) {
            (void)file_seek(mapping->backing_fd, saved, SEEK_SET);
            if (restore_protection) {
                (void)syscall3(SYSCALL_MPROTECT,
                               (uint64_t)(uintptr_t)mapping->address,
                               (uint64_t)mapping->length,
                               (uint64_t)(unsigned int)mapping->prot);
            }
            errno = EIO;
            return -1;
        }
        total += (size_t)written;
    }
    (void)file_seek(mapping->backing_fd, saved, SEEK_SET);
    if (restore_protection) {
        (void)syscall3(SYSCALL_MPROTECT,
                       (uint64_t)(uintptr_t)mapping->address,
                       (uint64_t)mapping->length,
                       (uint64_t)(unsigned int)mapping->prot);
    }
    return 0;
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    extern void* os_mmap(uint64_t length, uint64_t flags);
    if (length == 0 || (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
        (flags & (MAP_SHARED | MAP_PRIVATE)) == 0 ||
        (flags & (MAP_SHARED | MAP_PRIVATE)) == (MAP_SHARED | MAP_PRIVATE) ||
        (flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS)) != 0 ||
        offset < 0 || (offset & 4095) != 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    (void)addr;
    int anonymous = (flags & MAP_ANONYMOUS) != 0;
    if ((!anonymous && fd < 0) || (anonymous && offset != 0)) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    libc_mapping_t* mapping = libc_mapping_allocate();
    if (!mapping) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    void* ptr = os_mmap((uint64_t)length, 0);
    if (!ptr) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(ptr, 0, length);
    int32_t backing_fd = -1;
    if (!anonymous) {
        int64_t saved = file_seek(fd, 0, SEEK_CUR);
        if (saved < 0 || file_seek(fd, offset, SEEK_SET) < 0) {
            libc_mapping_discard_pages(ptr, length);
            errno = EBADF;
            return MAP_FAILED;
        }
        size_t total = 0;
        while (total < length) {
            int64_t count = file_read(
                fd, (uint8_t*)ptr + total, (uint64_t)(length - total));
            if (count < 0) {
                (void)file_seek(fd, saved, SEEK_SET);
                libc_mapping_discard_pages(ptr, length);
                errno = (int)-count;
                return MAP_FAILED;
            }
            if (count == 0) break;
            total += (size_t)count;
        }
        (void)file_seek(fd, saved, SEEK_SET);
        if ((flags & MAP_SHARED) != 0) {
            backing_fd = file_dup(fd);
            if (backing_fd < 0) {
                libc_mapping_discard_pages(ptr, length);
                errno = EMFILE;
                return MAP_FAILED;
            }
        }
    }
    memset(mapping, 0, sizeof(*mapping));
    mapping->address = ptr;
    mapping->length = length;
    mapping->backing_fd = backing_fd;
    mapping->offset = offset;
    mapping->shared = (flags & MAP_SHARED) != 0;
    mapping->prot = PROT_READ | PROT_WRITE;
    if (mprotect(ptr, length, prot) < 0) {
        if (backing_fd >= 0) (void)file_close(backing_fd);
        memset(mapping, 0, sizeof(*mapping));
        libc_mapping_discard_pages(ptr, length);
        return MAP_FAILED;
    }
    return ptr;
}

int munmap(void* addr, size_t length)
{
    libc_mapping_t* mapping = libc_mapping_find(addr, length);
    if (!mapping) {
        errno = EINVAL;
        return -1;
    }
    if (libc_mapping_flush(mapping) < 0) return -1;
    int64_t result = (int64_t)syscall2(
        SYSCALL_MUNMAP, (uint64_t)(uintptr_t)addr, (uint64_t)length);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    if (mapping->backing_fd >= 0) (void)file_close(mapping->backing_fd);
    memset(mapping, 0, sizeof(*mapping));
    return 0;
}

int mprotect(void* addr, size_t length, int prot)
{
    int64_t result = (int64_t)syscall3(
        SYSCALL_MPROTECT, (uint64_t)(uintptr_t)addr,
        (uint64_t)length, (uint64_t)(unsigned int)prot);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    libc_mapping_t* mapping = libc_mapping_find(addr, length);
    if (mapping) mapping->prot = prot;
    return 0;
}

int msync(void* addr, size_t length, int flags)
{
    if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
        ((flags & MS_ASYNC) != 0 && (flags & MS_SYNC) != 0)) {
        errno = EINVAL;
        return -1;
    }
    libc_mapping_t* mapping = libc_mapping_find(addr, length);
    if (!mapping) {
        errno = ENOMEM;
        return -1;
    }
    return libc_mapping_flush(mapping);
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    (void)fd;
    va_start(ap, request);
    if (request == FIONBIO) {
        int* param = va_arg(ap, int*);
        int flags;
        va_end(ap);
        if (!param) {
            errno = EINVAL;
            return -1;
        }
        flags = fcntl(fd, F_GETFL);
        if (flags < 0) {
            return -1;
        }
        if (*param) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return fcntl(fd, F_SETFL, flags);
    }
    va_end(ap);
    errno = ENOTSUP;
    return -1;
}

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
           struct timeval* timeout)
{
    if (nfds < 0 || nfds > FD_SETSIZE ||
        (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
                     timeout->tv_usec >= 1000000))) {
        errno = EINVAL;
        return -1;
    }
    fd_set requested_read;
    fd_set requested_write;
    fd_set requested_except;
    if (readfds) requested_read = *readfds;
    if (writefds) requested_write = *writefds;
    if (exceptfds) requested_except = *exceptfds;

    for (int fd = 0; fd < nfds; ++fd) {
        if ((readfds && FD_ISSET(fd, &requested_read)) ||
            (writefds && FD_ISSET(fd, &requested_write)) ||
            (exceptfds && FD_ISSET(fd, &requested_except))) {
            posix_fd_state_t* state = posix_fd_state(fd);
            if (!state || !state->valid) {
                errno = EBADF;
                return -1;
            }
        }
    }

    uint64_t wait_ms = 0;
    timeval_to_ms(timeout, &wait_ms);
    uint64_t deadline = get_uptime_ms() + wait_ms;
    for (;;) {
        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
        int count = 0;
        for (int fd = 0; fd < nfds; ++fd) {
            uint32_t requested = 0u;
            if (readfds && FD_ISSET(fd, &requested_read)) requested |= POLLIN;
            if (writefds && FD_ISSET(fd, &requested_write)) requested |= POLLOUT;
            if (exceptfds && FD_ISSET(fd, &requested_except))
                requested |= POLLERR | POLLHUP;
            if (requested == 0u) continue;
            uint32_t ready = (uint32_t)syscall2(
                SYSCALL_FD_POLL, (uint64_t)(int64_t)fd, requested);
            int fd_ready = 0;
            if (readfds && FD_ISSET(fd, &requested_read) &&
                (ready & (POLLIN | POLLHUP)) != 0u) {
                FD_SET(fd, readfds);
                fd_ready = 1;
            }
            if (writefds && FD_ISSET(fd, &requested_write) &&
                (ready & POLLOUT) != 0u) {
                FD_SET(fd, writefds);
                fd_ready = 1;
            }
            if (exceptfds && FD_ISSET(fd, &requested_except) &&
                (ready & (POLLERR | POLLHUP)) != 0u) {
                FD_SET(fd, exceptfds);
                fd_ready = 1;
            }
            if (fd_ready) ++count;
        }
        if (count != 0 || (timeout && get_uptime_ms() >= deadline)) return count;
        sleep_ms(1u);
    }
}

int poll(struct pollfd* fds, nfds_t nfds, int timeout)
{
    if ((!fds && nfds != 0u) || timeout < -1) {
        errno = EINVAL;
        return -1;
    }
    uint64_t deadline = get_uptime_ms() +
        (timeout >= 0 ? (uint64_t)timeout : 0u);
    for (;;) {
        int count = 0;
        for (nfds_t i = 0; i < nfds; ++i) {
            posix_fd_state_t* state = posix_fd_state(fds[i].fd);
            fds[i].revents = 0;
            if (fds[i].fd < 0) continue;
            if (!state || !state->valid) {
                fds[i].revents = POLLNVAL;
            } else {
                uint32_t requested = (uint32_t)(uint16_t)fds[i].events |
                                     POLLERR | POLLHUP;
                fds[i].revents = (short)(uint16_t)syscall2(
                    SYSCALL_FD_POLL, (uint64_t)(int64_t)fds[i].fd, requested);
            }
            if (fds[i].revents != 0) ++count;
        }
        if (count != 0 || (timeout >= 0 && get_uptime_ms() >= deadline))
            return count;
        sleep_ms(1u);
    }
}

int socket(int domain, int type, int protocol)
{
    if (domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (type != SOCK_STREAM || (protocol != 0 && protocol != 6)) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    int fd = socket_create(type);
    if (fd >= 0) {
        posix_fd_mark_open(fd, 0);
        posix_fd_state(fd)->type = POSIX_FD_SOCKET;
    }
    return fd;
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
    if (!addr || addrlen < sizeof(*in) || in->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    int result = socket_connect(
        sockfd, ntohl(in->sin_addr.s_addr), ntohs(in->sin_port));
    if (result < 0) return result;
    uint64_t deadline = get_uptime_ms() + 10000u;
    for (;;) {
        socket_info_t info;
        if (socket_get_info(sockfd, &info) < 0) {
            errno = EIO;
            return -1;
        }
        if (info.state == 4u) return 0;
        if (info.state == 0u || get_uptime_ms() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        sleep_ms(1u);
    }
}

int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
    if (!addr || addrlen < sizeof(*in) || in->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return socket_bind(sockfd, ntohs(in->sin_port));
}

int listen(int sockfd, int backlog)
{
    if (backlog < 0) {
        errno = EINVAL;
        return -1;
    }
    return socket_listen_with_backlog(sockfd, backlog);
}

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
    int fd = socket_accept(sockfd);
    if (fd >= 0) {
        posix_fd_mark_open(fd, 0);
        posix_fd_state(fd)->type = POSIX_FD_SOCKET;
        if (addr != NULL || addrlen != NULL) {
            if (!addr || !addrlen || *addrlen < sizeof(struct sockaddr_in)) {
                closesocket(fd);
                errno = EINVAL;
                return -1;
            }
            socket_info_t info;
            if (socket_get_info(fd, &info) < 0) {
                closesocket(fd);
                errno = EIO;
                return -1;
            }
            struct sockaddr_in peer;
            memset(&peer, 0, sizeof(peer));
            peer.sin_family = AF_INET;
            peer.sin_addr.s_addr = htonl(info.remote_ip);
            peer.sin_port = htons(info.remote_port);
            memcpy(addr, &peer, sizeof(peer));
            *addrlen = sizeof(peer);
        }
    }
    return fd;
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags)
{
    if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0) {
        errno = EINVAL;
        return -1;
    }
    return (ssize_t)socket_send(sockfd, buf, (uint32_t)len);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags)
{
    if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int32_t result = socket_recv(sockfd, buf, (uint32_t)len);
        if (result != 0 || (flags & MSG_DONTWAIT) != 0) {
            return (ssize_t)result;
        }
        socket_info_t info;
        if (socket_get_info(sockfd, &info) < 0 ||
            (info.state != 4u && info.state != 5u && info.state != 6u)) {
            return 0;
        }
        sleep_ms(1u);
    }
}

int closesocket(int sockfd)
{
    int rc = socket_close(sockfd);
    if (rc == 0) posix_fd_mark_closed(sockfd);
    return rc;
}

int shutdown(int sockfd, int how)
{
    if (how < SHUT_RD || how > SHUT_RDWR) {
        errno = EINVAL;
        return -1;
    }
    return socket_shutdown(sockfd, how);
}

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen)
{
    if (!optval || optlen < (socklen_t)sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    int32_t result = socket_set_option(
        sockfd, level, optname, *(const int*)optval);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
    if (!optval || !optlen || *optlen < (socklen_t)sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    int32_t result = socket_get_option(
        sockfd, level, optname, (int32_t*)optval);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    *optlen = (socklen_t)sizeof(int);
    return 0;
}

int getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
    if (!addr || !addrlen || *addrlen < sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    socket_info_t info;
    if (socket_get_info(sockfd, &info) < 0) {
        errno = EBADF;
        return -1;
    }
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(info.local_ip);
    local.sin_port = htons(info.local_port);
    memcpy(addr, &local, sizeof(local));
    *addrlen = sizeof(local);
    return 0;
}

int getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
    if (!addr || !addrlen || *addrlen < sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    socket_info_t info;
    if (socket_get_info(sockfd, &info) < 0 || info.remote_port == 0u) {
        errno = ENOTCONN;
        return -1;
    }
    struct sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(info.remote_ip);
    peer.sin_port = htons(info.remote_port);
    memcpy(addr, &peer, sizeof(peer));
    *addrlen = sizeof(peer);
    return 0;
}

uint16_t htons(uint16_t hostshort)
{
    return byte_swap16(hostshort);
}

uint16_t ntohs(uint16_t netshort)
{
    return byte_swap16(netshort);
}

uint32_t htonl(uint32_t hostlong)
{
    return byte_swap32(hostlong);
}

uint32_t ntohl(uint32_t netlong)
{
    return byte_swap32(netlong);
}

int inet_aton(const char* cp, struct in_addr* inp)
{
    unsigned long parts[4];
    char* end;
    int i;

    if (!cp || !inp) {
        errno = EINVAL;
        return 0;
    }

    for (i = 0; i < 4; ++i) {
        parts[i] = strtoul(cp, &end, 10);
        if (end == cp || parts[i] > 255) {
            return 0;
        }
        if (i < 3) {
            if (*end != '.') {
                return 0;
            }
            cp = end + 1;
        } else if (*end != '\0') {
            return 0;
        }
    }

    inp->s_addr = htonl(((uint32_t)parts[0] << 24) |
                        ((uint32_t)parts[1] << 16) |
                        ((uint32_t)parts[2] << 8) |
                        (uint32_t)parts[3]);
    return 1;
}

in_addr_t inet_addr(const char* cp)
{
    struct in_addr addr;
    if (!inet_aton(cp, &addr)) {
        return (in_addr_t)0xFFFFFFFFu;
    }
    return addr.s_addr;
}

#define LIBC_MAX_THREADS 128
#define LIBC_TLS_MAX_KEYS 64
#define FUTEX_WAIT 0ULL
#define FUTEX_WAKE 1ULL

typedef struct {
    pthread_t tid;
    void* (*routine)(void*);
    void* arg;
    void* retval;
    volatile int done;
    int detached;
    int cancel_state;
    int creator_ready;
    volatile int cleanup_claimed;
} libc_thread_desc_t;

typedef struct {
    pthread_t tid;
    void* slots[LIBC_TLS_MAX_KEYS];
    sigset_t signal_mask;
    sigset_t signal_pending;
} libc_tls_record_t;

static libc_thread_desc_t* g_libc_threads[LIBC_MAX_THREADS];
static libc_tls_record_t g_libc_tls[LIBC_MAX_THREADS];
static void (*g_libc_tls_destructors[LIBC_TLS_MAX_KEYS])(void*);
static pthread_key_t g_libc_next_key = 1;
static volatile int g_libc_thread_lock;

static void libc_thread_lock(void)
{
    while (__sync_lock_test_and_set(&g_libc_thread_lock, 1)) {
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

static void libc_thread_unlock(void)
{
    __sync_lock_release(&g_libc_thread_lock);
}

static int libc_futex_wait(volatile int* address, int expected,
                           uint64_t timeout_ns)
{
    return (int)(int64_t)syscall4(SYSCALL_FUTEX,
                                  (uint64_t)(uintptr_t)address,
                                  FUTEX_WAIT,
                                  (uint64_t)(uint32_t)expected,
                                  timeout_ns);
}

static void libc_futex_wake(volatile int* address, int count)
{
    (void)syscall4(SYSCALL_FUTEX,
                   (uint64_t)(uintptr_t)address,
                   FUTEX_WAKE,
                   (uint64_t)(uint32_t)count,
                   0);
}

static libc_thread_desc_t* libc_thread_find_locked(pthread_t tid)
{
    for (int i = 0; i < LIBC_MAX_THREADS; ++i) {
        if (g_libc_threads[i] != NULL &&
            g_libc_threads[i]->tid == tid) {
            return g_libc_threads[i];
        }
    }
    return NULL;
}

static int libc_thread_add(libc_thread_desc_t* desc)
{
    int result = -1;
    libc_thread_lock();
    for (int i = 0; i < LIBC_MAX_THREADS; ++i) {
        if (g_libc_threads[i] == NULL) {
            g_libc_threads[i] = desc;
            result = 0;
            break;
        }
    }
    libc_thread_unlock();
    return result;
}

static void libc_thread_remove_locked(libc_thread_desc_t* desc)
{
    for (int i = 0; i < LIBC_MAX_THREADS; ++i) {
        if (g_libc_threads[i] == desc) {
            g_libc_threads[i] = NULL;
            return;
        }
    }
}

static void libc_thread_cleanup(libc_thread_desc_t* desc)
{
    if (desc != NULL &&
        __sync_bool_compare_and_swap(&desc->cleanup_claimed, 0, 1)) {
        free(desc);
    }
}

static libc_tls_record_t* libc_tls_find_locked(pthread_t tid)
{
    for (int i = 0; i < LIBC_MAX_THREADS; ++i) {
        if (g_libc_tls[i].tid == tid) {
            return &g_libc_tls[i];
        }
    }
    return NULL;
}

static libc_tls_record_t* libc_tls_alloc_locked(pthread_t tid)
{
    libc_tls_record_t* record = libc_tls_find_locked(tid);
    if (record != NULL) {
        return record;
    }
    for (int i = 0; i < LIBC_MAX_THREADS; ++i) {
        if (g_libc_tls[i].tid == 0) {
            g_libc_tls[i].tid = tid;
            return &g_libc_tls[i];
        }
    }
    return NULL;
}

static void libc_tls_cleanup(pthread_t tid)
{
    for (int iteration = 0; iteration < 4; ++iteration) {
        int called = 0;
        for (pthread_key_t key = 1; key < LIBC_TLS_MAX_KEYS; ++key) {
            void* value = NULL;
            void (*destructor)(void*) = NULL;
            libc_thread_lock();
            libc_tls_record_t* record = libc_tls_find_locked(tid);
            if (record != NULL && record->slots[key] != NULL) {
                value = record->slots[key];
                record->slots[key] = NULL;
                destructor = g_libc_tls_destructors[key];
            }
            libc_thread_unlock();
            if (value != NULL && destructor != NULL) {
                destructor(value);
                called = 1;
            }
        }
        if (!called) {
            break;
        }
    }

    libc_thread_lock();
    libc_tls_record_t* record = libc_tls_find_locked(tid);
    if (record != NULL) {
        memset(record, 0, sizeof(*record));
    }
    libc_thread_unlock();
}

static void libc_thread_entry(libc_thread_desc_t* desc)
{
    if (desc == NULL) {
        (void)syscall1(SYSCALL_THREAD_EXIT, 0);
        for (;;) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
    }

    pthread_t self = (pthread_t)syscall0(SYSCALL_GETTID);
    libc_thread_lock();
    (void)libc_tls_alloc_locked(self);
    libc_thread_unlock();

    void* result = desc->routine(desc->arg);
    libc_tls_cleanup(self);
    int cleanup = 0;
    libc_thread_lock();
    desc->retval = result;
    __sync_synchronize();
    desc->done = 1;
    cleanup = desc->detached && desc->creator_ready;
    libc_futex_wake(&desc->done, 0x7fffffff);
    libc_thread_unlock();

    if (cleanup) {
        libc_thread_cleanup(desc);
    }
    (void)syscall1(SYSCALL_THREAD_EXIT, 0);
    for (;;) {
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

pthread_t pthread_self(void)
{
    return (pthread_t)syscall0(SYSCALL_GETTID);
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg)
{
    if (thread == NULL || start_routine == NULL) {
        return EINVAL;
    }

    libc_thread_desc_t* desc =
        (libc_thread_desc_t*)calloc(1, sizeof(*desc));
    if (desc == NULL) {
        return ENOMEM;
    }
    desc->routine = start_routine;
    desc->arg = arg;
    desc->detached = attr != NULL && attr->detached != 0;
    desc->cancel_state = PTHREAD_CANCEL_ENABLE;
    desc->creator_ready = desc->detached ? 0 : 1;
    int detached = desc->detached;

    if (!detached && libc_thread_add(desc) < 0) {
        free(desc);
        return EAGAIN;
    }

    int64_t tid = (int64_t)syscall2(SYSCALL_THREAD_CREATE,
                                    (uint64_t)(uintptr_t)libc_thread_entry,
                                    (uint64_t)(uintptr_t)desc);
    if (tid < 0) {
        if (!detached) {
            libc_thread_lock();
            libc_thread_remove_locked(desc);
            libc_thread_unlock();
        }
        free(desc);
        return EAGAIN;
    }

    desc->tid = (pthread_t)tid;
    *thread = desc->tid;
    if (detached) {
        (void)syscall1(SYSCALL_THREAD_DETACH, (uint64_t)tid);
        libc_thread_lock();
        desc->creator_ready = 1;
        int done = desc->done;
        libc_thread_unlock();
        if (done) {
            libc_thread_cleanup(desc);
        }
    }
    return 0;
}

int pthread_join(pthread_t thread, void** retval)
{
    if (thread == 0 || thread == pthread_self()) {
        return EINVAL;
    }

    libc_thread_lock();
    libc_thread_desc_t* desc = libc_thread_find_locked(thread);
    if (desc == NULL || desc->detached) {
        libc_thread_unlock();
        return ESRCH;
    }
    libc_thread_unlock();

    while (!desc->done) {
        (void)libc_futex_wait(&desc->done, 0, 0);
    }

    int result;
    do {
        result = (int)(int64_t)syscall1(SYSCALL_THREAD_JOIN,
                                        (uint64_t)thread);
        if (result == -EAGAIN) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
    } while (result == -EAGAIN);
    if (result < 0) {
        return -result;
    }

    if (retval != NULL) {
        *retval = desc->retval;
    }
    libc_thread_lock();
    libc_thread_remove_locked(desc);
    libc_thread_unlock();
    libc_thread_cleanup(desc);
    return 0;
}

int pthread_detach(pthread_t thread)
{
    if (thread == 0) {
        return EINVAL;
    }

    libc_thread_lock();
    libc_thread_desc_t* desc = libc_thread_find_locked(thread);
    if (desc == NULL || desc->detached) {
        libc_thread_unlock();
        return ESRCH;
    }
    desc->detached = 1;
    desc->creator_ready = 1;
    libc_thread_remove_locked(desc);
    int done = desc->done;
    libc_thread_unlock();

    int result = (int)(int64_t)syscall1(SYSCALL_THREAD_DETACH,
                                        (uint64_t)thread);
    if (done) {
        libc_thread_cleanup(desc);
    }
    return result < 0 ? -result : 0;
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

int pthread_cancel(pthread_t thread)
{
    (void)thread;
    return ENOTSUP;
}

int pthread_setcancelstate(int state, int* oldstate)
{
    static int main_cancel_state = PTHREAD_CANCEL_ENABLE;
    if (state != PTHREAD_CANCEL_ENABLE &&
        state != PTHREAD_CANCEL_DISABLE) {
        return EINVAL;
    }

    int* cancel_state = &main_cancel_state;
    libc_thread_lock();
    libc_thread_desc_t* desc = libc_thread_find_locked(pthread_self());
    if (desc != NULL) {
        cancel_state = &desc->cancel_state;
    }
    if (oldstate != NULL) {
        *oldstate = *cancel_state;
    }
    *cancel_state = state;
    libc_thread_unlock();
    return 0;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
    if (once_control == NULL || init_routine == NULL) {
        return EINVAL;
    }
    if (__sync_bool_compare_and_swap(&once_control->done, 0, 1)) {
        init_routine();
        __sync_synchronize();
        once_control->done = 2;
        libc_futex_wake(&once_control->done, 0x7fffffff);
    } else {
        while (once_control->done != 2) {
            (void)libc_futex_wait(&once_control->done, 1, 0);
        }
    }
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
{
    if (mutex == NULL) {
        return EINVAL;
    }
    mutex->locked = 0;
    mutex->type = attr != NULL ? attr->type : PTHREAD_MUTEX_NORMAL;
    mutex->owner = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    if (mutex == NULL || mutex->locked != 0) {
        return mutex == NULL ? EINVAL : EBUSY;
    }
    mutex->owner = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (mutex == NULL) {
        return EINVAL;
    }
    pthread_t self = pthread_self();
    if (mutex->locked != 0 && mutex->owner == self) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            ++mutex->locked;
            return 0;
        }
        if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
            return EBUSY;
        }
    }

    for (;;) {
        if (__sync_bool_compare_and_swap(&mutex->locked, 0, 1)) {
            break;
        }
        int observed = mutex->locked;
        (void)libc_futex_wait(&mutex->locked, observed, 0);
    }
    mutex->owner = self;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex)
{
    if (mutex == NULL) {
        return EINVAL;
    }
    pthread_t self = pthread_self();
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE &&
        mutex->locked != 0 && mutex->owner == self) {
        ++mutex->locked;
        return 0;
    }
    if (!__sync_bool_compare_and_swap(&mutex->locked, 0, 1)) {
        return EBUSY;
    }
    mutex->owner = self;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    if (mutex == NULL) {
        return EINVAL;
    }
    if (mutex->owner != pthread_self()) {
        return EPERM;
    }
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->locked > 1) {
        --mutex->locked;
        return 0;
    }
    mutex->owner = 0;
    __sync_lock_release(&mutex->locked);
    libc_futex_wake(&mutex->locked, 1);
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr)
{
    if (attr == NULL) {
        return EINVAL;
    }
    attr->pshared = 0;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr)
{
    if (attr == NULL) {
        return EINVAL;
    }
    attr->pshared = 0;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type)
{
    if (attr == NULL ||
        (type != PTHREAD_MUTEX_NORMAL &&
         type != PTHREAD_MUTEX_RECURSIVE &&
         type != PTHREAD_MUTEX_ERRORCHECK)) {
        return EINVAL;
    }
    attr->type = type;
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr)
{
    (void)attr;
    if (cond == NULL) {
        return EINVAL;
    }
    cond->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    if (cond == NULL) {
        return EINVAL;
    }
    cond->seq = 0;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex)
{
    if (cond == NULL || mutex == NULL) {
        return EINVAL;
    }
    unsigned seq = cond->seq;
    int result = pthread_mutex_unlock(mutex);
    if (result != 0) {
        return result;
    }
    while (cond->seq == seq) {
        (void)libc_futex_wait((volatile int*)&cond->seq, (int)seq, 0);
    }
    return pthread_mutex_lock(mutex);
}

int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                           const struct timespec* abstime)
{
    if (cond == NULL || mutex == NULL || abstime == NULL ||
        abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
        abstime->tv_nsec >= 1000000000L) {
        return EINVAL;
    }

    unsigned seq = cond->seq;
    int result = pthread_mutex_unlock(mutex);
    if (result != 0) {
        return result;
    }
    while (cond->seq == seq) {
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) < 0) {
            (void)pthread_mutex_lock(mutex);
            return EIO;
        }
        int64_t remaining_sec =
            (int64_t)abstime->tv_sec - (int64_t)now.tv_sec;
        int64_t remaining_ns =
            remaining_sec * 1000000000LL +
            ((int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec);
        if (remaining_ns <= 0) {
            (void)pthread_mutex_lock(mutex);
            return ETIMEDOUT;
        }
        (void)libc_futex_wait((volatile int*)&cond->seq, (int)seq,
                              (uint64_t)remaining_ns);
    }
    return pthread_mutex_lock(mutex);
}

int pthread_cond_signal(pthread_cond_t* cond)
{
    if (cond == NULL) {
        return EINVAL;
    }
    __sync_add_and_fetch(&cond->seq, 1);
    libc_futex_wake((volatile int*)&cond->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* cond)
{
    if (cond == NULL) {
        return EINVAL;
    }
    __sync_add_and_fetch(&cond->seq, 1);
    libc_futex_wake((volatile int*)&cond->seq, 0x7fffffff);
    return 0;
}

int pthread_condattr_init(pthread_condattr_t* attr)
{
    if (attr == NULL) {
        return EINVAL;
    }
    attr->pshared = 0;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr)
{
    if (attr == NULL) {
        return EINVAL;
    }
    attr->pshared = 0;
    return 0;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*))
{
    if (key == NULL) {
        return EINVAL;
    }
    libc_thread_lock();
    if (g_libc_next_key >= LIBC_TLS_MAX_KEYS) {
        libc_thread_unlock();
        return EAGAIN;
    }
    pthread_key_t new_key = g_libc_next_key++;
    g_libc_tls_destructors[new_key] = destructor;
    *key = new_key;
    libc_thread_unlock();
    return 0;
}

int pthread_key_delete(pthread_key_t key)
{
    if (key == 0 || key >= LIBC_TLS_MAX_KEYS) {
        return EINVAL;
    }
    libc_thread_lock();
    g_libc_tls_destructors[key] = NULL;
    for (int i = 0; i < LIBC_MAX_THREADS; ++i) {
        g_libc_tls[i].slots[key] = NULL;
    }
    libc_thread_unlock();
    return 0;
}

void* pthread_getspecific(pthread_key_t key)
{
    if (key == 0 || key >= LIBC_TLS_MAX_KEYS) {
        return NULL;
    }
    libc_thread_lock();
    libc_tls_record_t* record = libc_tls_find_locked(pthread_self());
    void* value = record != NULL ? record->slots[key] : NULL;
    libc_thread_unlock();
    return value;
}

int pthread_setspecific(pthread_key_t key, const void* value)
{
    if (key == 0 || key >= LIBC_TLS_MAX_KEYS) {
        return EINVAL;
    }
    libc_thread_lock();
    libc_tls_record_t* record = libc_tls_alloc_locked(pthread_self());
    if (record == NULL) {
        libc_thread_unlock();
        return ENOMEM;
    }
    record->slots[key] = (void*)value;
    libc_thread_unlock();
    return 0;
}

char **environ = NULL;

extern int32_t process_spawn(const char *path);

int execve(const char *path, char *const argv[], char *const envp[])
{
    (void)argv;
    (void)envp;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = process_spawn(path);
    if (r < 0) {
        errno = ENOENT;
        return -1;
    }
    process_exit(0);
    return -1;
}

int execv(const char *path, char *const argv[])
{
    return execve(path, argv, NULL);
}

int execvp(const char *file, char *const argv[])
{
    return execve(file, argv, NULL);
}

int kill(pid_t pid, int sig)
{
    if (pid <= 0 || sig < 0 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    int64_t result = (int64_t)syscall2(
        SYSCALL_TKILL, (uint64_t)(int64_t)pid, (uint64_t)(uint32_t)sig);
    if (result < 0) {
        errno = (int)-result;
        return -1;
    }
    return 0;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

static struct sigaction g_libc_sigact[NSIG];

static libc_tls_record_t* libc_signal_state(void)
{
    libc_thread_lock();
    libc_tls_record_t* state = libc_tls_alloc_locked(pthread_self());
    libc_thread_unlock();
    return state;
}

static void libc_signal_trampoline(int signum)
{
    if (signum < 1 || signum >= NSIG) return;
    libc_tls_record_t* state = libc_signal_state();
    if (state == NULL) return;
    if (state->signal_mask & (1ULL << (signum - 1))) {
        state->signal_pending |= (1ULL << (signum - 1));
        return;
    }
    struct sigaction *sa = &g_libc_sigact[signum];
    if (sa->sa_handler == SIG_IGN) return;
    if (sa->sa_handler == SIG_DFL) {
        if (signum != SIGCHLD && signum != SIGURG) {
            _exit(128 + signum);
        }
        return;
    }
    sigset_t saved = state->signal_mask;
    state->signal_mask |= sa->sa_mask;
    if (!(sa->sa_flags & SA_NODEFER)) {
        state->signal_mask |= (1ULL << (signum - 1));
    }
    if (sa->sa_flags & SA_RESETHAND) {
        sighandler_t old = sa->sa_handler;
        sa->sa_handler = SIG_DFL;
        old(signum);
    } else {
        sa->sa_handler(signum);
    }
    state->signal_mask = saved;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (signum < 1 || signum >= NSIG ||
        signum == SIGKILL || signum == SIGSTOP)
    {
        errno = EINVAL;
        return -1;
    }
    if (oldact) {
        memcpy(oldact, &g_libc_sigact[signum], sizeof(struct sigaction));
    }
    if (act) {
        memcpy(&g_libc_sigact[signum], act, sizeof(struct sigaction));
        os_signal(signum, libc_signal_trampoline);
    }
    return 0;
}

sighandler_t signal(int signum, sighandler_t handler)
{
    struct sigaction action;
    struct sigaction previous;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    if (sigaction(signum, &action, &previous) < 0) return SIG_ERR;
    return previous.sa_handler;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    libc_tls_record_t* state = libc_signal_state();
    if (state == NULL) { errno = ENOMEM; return -1; }
    if (oldset) *oldset = state->signal_mask;
    if (!set) return 0;
    switch (how) {
        case SIG_BLOCK:
            state->signal_mask |= *set; break;
        case SIG_UNBLOCK:
            state->signal_mask &= ~(*set);
            {
                sigset_t d =
                    state->signal_pending & ~state->signal_mask;
                for (int s = 1; s < NSIG; s++) {
                    if (d & (1ULL << (s - 1))) {
                        state->signal_pending &= ~(1ULL << (s - 1));
                        libc_signal_trampoline(s);
                    }
                }
            }
            break;
        case SIG_SETMASK:
            state->signal_mask = *set; break;
        default:
            errno = EINVAL; return -1;
    }
    return 0;
}

int sigpending(sigset_t *set)
{
    if (!set) { errno = EINVAL; return -1; }
    libc_tls_record_t* state = libc_signal_state();
    if (state == NULL) { errno = ENOMEM; return -1; }
    *set = state->signal_pending;
    return 0;
}

int raise(int sig)
{
    if (sig < 1 || sig >= NSIG) { errno = EINVAL; return -1; }
    libc_signal_trampoline(sig);
    return 0;
}

typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_for_clock_t;

static int is_leap_year(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month_c(int m, int y)
{
    static const int dim[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && is_leap_year(y)) return 29;
    return dim[m - 1];
}

static int64_t date_to_epoch_secs(int year, int mon, int day,
                                   int hour, int min, int sec)
{
    int64_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int m = 1; m < mon; m++) {
        days += days_in_month_c(m, year);
    }
    days += day - 1;
    return days * 86400LL + (int64_t)hour * 3600LL +
           (int64_t)min * 60LL + (int64_t)sec;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (!tp) { errno = EINVAL; return -1; }
    if (clk_id == CLOCK_MONOTONIC ||
        clk_id == CLOCK_PROCESS_CPUTIME_ID ||
        clk_id == CLOCK_THREAD_CPUTIME_ID)
    {
        uint64_t ms = get_uptime_ms();
        tp->tv_sec  = (time_t)(ms / 1000ULL);
        tp->tv_nsec = (long)((ms % 1000ULL) * 1000000L);
        return 0;
    }
    if (clk_id == CLOCK_REALTIME) {
        rtc_time_t rtc;
        if (sys_get_rtc_time(&rtc) == 0) {
            tp->tv_sec  = (time_t)date_to_epoch_secs(
                (int)rtc.year, (int)rtc.month,  (int)rtc.day,
                (int)rtc.hour, (int)rtc.minute, (int)rtc.second);
            tp->tv_nsec = 0;
            return 0;
        }
        errno = EIO;
        return -1;
    }
    errno = EINVAL;
    return -1;
}

int clock_settime(clockid_t clk_id, const struct timespec *tp)
{
    (void)clk_id; (void)tp;
    errno = ENOTSUP;
    return -1;
}

int clock_getres(clockid_t clk_id, struct timespec *res)
{
    (void)clk_id;
    if (!res) { errno = EINVAL; return -1; }
    res->tv_sec  = 0;
    res->tv_nsec = 1000000L;
    return 0;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    if (!timep || !result) return NULL;
    memset(result, 0, sizeof(*result));
    int64_t t   = (int64_t)*timep;
    int64_t rem = t % 86400LL;
    int64_t days = t / 86400LL;
    if (rem < 0) { rem += 86400LL; days--; }
    result->tm_sec  = (int)(rem % 60); rem /= 60;
    result->tm_min  = (int)(rem % 60);
    result->tm_hour = (int)(rem / 60);
    result->tm_wday = (int)((days + 4) % 7);
    if (result->tm_wday < 0) result->tm_wday += 7;
    int year = 1970;
    if (days >= 0) {
        while (1) {
            int dy = is_leap_year(year) ? 366 : 365;
            if (days < dy) break;
            days -= dy; year++;
        }
    } else {
        while (days < 0) {
            year--;
            days += is_leap_year(year) ? 366 : 365;
        }
    }
    result->tm_year = year - 1900;
    result->tm_yday = (int)days;
    int mon = 1;
    while (days >= days_in_month_c(mon, year)) {
        days -= days_in_month_c(mon, year);
        mon++;
    }
    result->tm_mon  = mon - 1;
    result->tm_mday = (int)days + 1;
    result->tm_isdst = 0;
    return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return gmtime_r(timep, result);
}

time_t mktime(struct tm *t)
{
    if (!t) return (time_t)-1;
    while (t->tm_mon < 0)  { t->tm_mon += 12; t->tm_year--; }
    while (t->tm_mon > 11) { t->tm_mon -= 12; t->tm_year++; }
    int year = t->tm_year + 1900;
    int mon  = t->tm_mon + 1;
    int day  = t->tm_mday;
    while (day <= 0) {
        mon--;
        if (mon < 1) { mon = 12; year--; }
        day += days_in_month_c(mon, year);
    }
    while (day > days_in_month_c(mon, year)) {
        day -= days_in_month_c(mon, year);
        if (++mon > 12) { mon = 1; year++; }
    }
    t->tm_year = year - 1900;
    t->tm_mon  = mon - 1;
    t->tm_mday = day;
    return (time_t)date_to_epoch_secs(year, mon, day,
                                       t->tm_hour, t->tm_min, t->tm_sec);
}

double difftime(time_t t1, time_t t0)
{
    return (double)(t1 - t0);
}
