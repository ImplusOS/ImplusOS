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
extern int32_t file_stat(const char* path, os_file_stat_t* stat_out);
extern int32_t file_pipe(int32_t fds[2]);
extern int32_t file_dup(int32_t oldfd);
extern int32_t file_dup2(int32_t oldfd, int32_t newfd);
extern int32_t socket_create(int32_t type);
extern int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port);
extern int32_t socket_bind(int32_t sockfd, uint16_t port);
extern int32_t socket_listen(int32_t sockfd);
extern int32_t socket_accept(int32_t sockfd);
extern int32_t socket_send(int32_t sockfd, const void* data, uint32_t len);
extern int32_t socket_recv(int32_t sockfd, void* buf, uint32_t buf_len);
extern int32_t socket_close(int32_t sockfd);
extern int32_t process_waitpid(int32_t pid, int32_t* status_out, int32_t options);
extern int32_t process_get_current_pid(void);
extern int32_t process_getppid(void);
extern void process_exit(int32_t status);
extern void sleep_ms(uint64_t milliseconds);
extern uint64_t get_uptime_ms(void);
extern int32_t sys_get_rtc_time(rtc_time_t* time);

#define POSIX_MAX_TRACKED_FDS 256

typedef struct {
    int valid;
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
    return (ssize_t)file_read(fd, buf, (uint64_t)count);
}

ssize_t write(int fd, const void* buf, size_t count)
{
    return (ssize_t)file_write(fd, buf, (uint64_t)count);
}

int close(int fd)
{
    int rc = file_close(fd);
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
    return file_pipe(pipefd);
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

    tv->tv_sec = (time_t)(get_uptime_ms() / 1000ULL);
    tv->tv_usec = (suseconds_t)((get_uptime_ms() % 1000ULL) * 1000ULL);
    return 0;
}

uint64_t clock_ms(void)
{
    return get_uptime_ms();
}

time_t time(time_t* out)
{
    time_t now = (time_t)(get_uptime_ms() / 1000ULL);
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

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    extern void* os_mmap(uint64_t length, uint64_t flags);
    void* ptr;
    (void)addr;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    ptr = os_mmap((uint64_t)length, 0);
    return ptr ? ptr : MAP_FAILED;
}

int munmap(void* addr, size_t length)
{
    (void)addr;
    (void)length;
    return 0;
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
    uint64_t wait_ms = 0;
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    timeval_to_ms(timeout, &wait_ms);
    if (wait_ms > 0) {
        sleep_ms(wait_ms);
    }
    return 0;
}

int poll(struct pollfd* fds, nfds_t nfds, int timeout)
{
    nfds_t i;
    if (timeout > 0) {
        sleep_ms((uint64_t)timeout);
    }
    for (i = 0; i < nfds; ++i) {
        posix_fd_state_t* state = posix_fd_state(fds[i].fd);
        fds[i].revents = 0;
        if (!state || !state->valid) {
            fds[i].revents = POLLERR;
            continue;
        }
        if (fds[i].events & POLLIN) {
            fds[i].revents |= POLLIN;
        }
        if (fds[i].events & POLLOUT) {
            fds[i].revents |= POLLOUT;
        }
    }
    return (int)nfds;
}

int socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    return socket_create(type);
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
    (void)addrlen;
    if (!addr || in->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return socket_connect(sockfd, ntohl(in->sin_addr.s_addr), ntohs(in->sin_port));
}

int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
    (void)addrlen;
    if (!addr || in->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    return socket_bind(sockfd, ntohs(in->sin_port));
}

int listen(int sockfd, int backlog)
{
    (void)backlog;
    return socket_listen(sockfd);
}

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen)
{
    (void)addr;
    (void)addrlen;
    return socket_accept(sockfd);
}

ssize_t send(int sockfd, const void* buf, size_t len, int flags)
{
    (void)flags;
    return (ssize_t)socket_send(sockfd, buf, (uint32_t)len);
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags)
{
    (void)flags;
    return (ssize_t)socket_recv(sockfd, buf, (uint32_t)len);
}

int closesocket(int sockfd)
{
    return socket_close(sockfd);
}

int shutdown(int sockfd, int how)
{
    (void)how;
    return socket_close(sockfd);
}

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen)
{
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    return 0;
}

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
    (void)sockfd;
    (void)level;
    if (optname == SO_ERROR && optval && optlen && *optlen >= (socklen_t)sizeof(int)) {
        *(int*)optval = 0;
        *optlen = (socklen_t)sizeof(int);
        return 0;
    }
    errno = ENOTSUP;
    return -1;
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

pthread_t pthread_self(void)
{
    return (pthread_t)(uintptr_t)getpid();
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg)
{
    (void)thread;
    (void)attr;
    (void)start_routine;
    (void)arg;
    return ENOTSUP;
}

int pthread_join(pthread_t thread, void** retval)
{
    (void)thread;
    (void)retval;
    return ENOTSUP;
}

int pthread_detach(pthread_t thread)
{
    (void)thread;
    return ENOTSUP;
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
    if (oldstate) {
        *oldstate = PTHREAD_CANCEL_ENABLE;
    }
    return (state == PTHREAD_CANCEL_ENABLE || state == PTHREAD_CANCEL_DISABLE) ? 0 : EINVAL;
}

int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
    if (!once_control || !init_routine) {
        return EINVAL;
    }
    if (__sync_bool_compare_and_swap(&once_control->done, 0, 1)) {
        init_routine();
    }
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr)
{
    if (!mutex) {
        return EINVAL;
    }
    mutex->locked = 0;
    mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
    mutex->owner = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex)
{
    if (!mutex) {
        return EINVAL;
    }
    mutex->locked = 0;
    mutex->owner = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    pthread_t self;
    if (!mutex) {
        return EINVAL;
    }
    self = pthread_self();
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutex->locked && mutex->owner == self) {
        return EBUSY;
    }
    while (__sync_lock_test_and_set(&mutex->locked, 1)) {
        sleep_ms(1);
    }
    mutex->owner = self;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    if (!mutex) {
        return EINVAL;
    }
    mutex->owner = 0;
    __sync_lock_release(&mutex->locked);
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr)
{
    if (!attr) {
        return EINVAL;
    }
    attr->pshared = 0;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr)
{
    if (!attr) {
        return EINVAL;
    }
    attr->pshared = 0;
    attr->type = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type)
{
    if (!attr) {
        return EINVAL;
    }
    attr->type = type;
    return 0;
}

int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr)
{
    (void)attr;
    if (!cond) {
        return EINVAL;
    }
    cond->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* cond)
{
    if (!cond) {
        return EINVAL;
    }
    cond->seq = 0;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex)
{
    unsigned seq;
    if (!cond || !mutex) {
        return EINVAL;
    }
    seq = cond->seq;
    pthread_mutex_unlock(mutex);
    while (cond->seq == seq) {
        sleep_ms(1);
    }
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                           const struct timespec* abstime)
{
    unsigned seq;
    uint64_t deadline_ms;
    if (!cond || !mutex || !abstime) {
        return EINVAL;
    }
    seq = cond->seq;
    deadline_ms = (uint64_t)abstime->tv_sec * 1000ULL + (uint64_t)(abstime->tv_nsec / 1000000L);
    pthread_mutex_unlock(mutex);
    while (cond->seq == seq) {
        if (get_uptime_ms() >= deadline_ms) {
            pthread_mutex_lock(mutex);
            return ETIMEDOUT;
        }
        sleep_ms(1);
    }
    pthread_mutex_lock(mutex);
    return 0;
}

int pthread_cond_signal(pthread_cond_t* cond)
{
    if (!cond) {
        return EINVAL;
    }
    __sync_add_and_fetch(&cond->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* cond)
{
    return pthread_cond_signal(cond);
}

int pthread_condattr_init(pthread_condattr_t* attr)
{
    if (!attr) {
        return EINVAL;
    }
    attr->pshared = 0;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr)
{
    if (!attr) {
        return EINVAL;
    }
    attr->pshared = 0;
    return 0;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*))
{
    static pthread_key_t next_key = 1;
    (void)destructor;
    if (!key) {
        return EINVAL;
    }
    *key = next_key++;
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
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}

pid_t wait(int *status)
{
    return waitpid(-1, status, 0);
}

static struct sigaction g_libc_sigact[NSIG];
static sigset_t         g_libc_sigmask = 0;
static sigset_t         g_libc_pending = 0;

static void libc_signal_trampoline(int signum)
{
    if (signum < 1 || signum >= NSIG) return;
    if (g_libc_sigmask & (1ULL << (signum - 1))) {
        g_libc_pending |= (1ULL << (signum - 1));
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
    sigset_t saved = g_libc_sigmask;
    g_libc_sigmask |= sa->sa_mask;
    if (!(sa->sa_flags & SA_NODEFER)) {
        g_libc_sigmask |= (1ULL << (signum - 1));
    }
    if (sa->sa_flags & SA_RESETHAND) {
        sighandler_t old = sa->sa_handler;
        sa->sa_handler = SIG_DFL;
        old(signum);
    } else {
        sa->sa_handler(signum);
    }
    g_libc_sigmask = saved;
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
        signal(signum, libc_signal_trampoline);
    }
    return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    if (oldset) *oldset = g_libc_sigmask;
    if (!set) return 0;
    switch (how) {
        case SIG_BLOCK:
            g_libc_sigmask |= *set; break;
        case SIG_UNBLOCK:
            g_libc_sigmask &= ~(*set);
            {
                sigset_t d = g_libc_pending & ~g_libc_sigmask;
                for (int s = 1; s < NSIG; s++) {
                    if (d & (1ULL << (s - 1))) {
                        g_libc_pending &= ~(1ULL << (s - 1));
                        libc_signal_trampoline(s);
                    }
                }
            }
            break;
        case SIG_SETMASK:
            g_libc_sigmask = *set; break;
        default:
            errno = EINVAL; return -1;
    }
    return 0;
}

int sigpending(sigset_t *set)
{
    if (!set) { errno = EINVAL; return -1; }
    *set = g_libc_pending;
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
        uint64_t ms = get_uptime_ms();
        tp->tv_sec  = (time_t)(ms / 1000ULL);
        tp->tv_nsec = (long)((ms % 1000ULL) * 1000000L);
        return 0;
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
