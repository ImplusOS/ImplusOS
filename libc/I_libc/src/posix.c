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
#include <semaphore.h>
#include <locale.h>
#include <wchar.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <regex.h>
#include <glob.h>
#include <fnmatch.h>
#include <getopt.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>

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
extern uint32_t dns_resolve(const char *hostname);

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

char* optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;
static const char* getopt_next = NULL;

static int getopt_missing_arg(const char* optstring)
{
    return (optstring && optstring[0] == ':') ? ':' : '?';
}

int getopt(int argc, char* const argv[], const char* optstring)
{
    optarg = NULL;

    if (!argv || !optstring || optind < 0) {
        return -1;
    }
    if (optind == 0) {
        optind = 1;
    }

    if (!getopt_next || *getopt_next == '\0') {
        if (optind >= argc || !argv[optind] || argv[optind][0] != '-' ||
            argv[optind][1] == '\0') {
            return -1;
        }
        if (strcmp(argv[optind], "--") == 0) {
            optind++;
            return -1;
        }
        getopt_next = argv[optind] + 1;
    }

    char c = *getopt_next++;
    const char* spec = strchr(optstring, c);
    if (!spec || c == ':') {
        optopt = (unsigned char)c;
        if (*getopt_next == '\0') {
            optind++;
            getopt_next = NULL;
        }
        return '?';
    }

    if (spec[1] == ':') {
        if (*getopt_next != '\0') {
            optarg = (char*)getopt_next;
            optind++;
            getopt_next = NULL;
        } else if (optind + 1 < argc) {
            optarg = argv[++optind];
            optind++;
            getopt_next = NULL;
        } else {
            optopt = (unsigned char)c;
            optind++;
            getopt_next = NULL;
            return getopt_missing_arg(optstring);
        }
    } else if (*getopt_next == '\0') {
        optind++;
        getopt_next = NULL;
    }

    return (unsigned char)c;
}

static int getopt_long_match(const char* arg, size_t name_len,
                             const struct option* longopts)
{
    if (!longopts) {
        return -1;
    }
    for (int i = 0; longopts[i].name; i++) {
        if (strlen(longopts[i].name) == name_len &&
            strncmp(arg, longopts[i].name, name_len) == 0) {
            return i;
        }
    }
    return -1;
}

int getopt_long(int argc, char* const argv[], const char* optstring,
                const struct option* longopts, int* longindex)
{
    if (!argv || optind < 0) {
        return -1;
    }
    if (optind == 0) {
        optind = 1;
    }

    if (optind < argc && argv[optind] && strncmp(argv[optind], "--", 2) == 0) {
        const char* arg = argv[optind] + 2;
        const char* eq;
        size_t name_len;
        int idx;

        if (*arg == '\0') {
            optind++;
            return -1;
        }

        eq = strchr(arg, '=');
        name_len = eq ? (size_t)(eq - arg) : strlen(arg);
        idx = getopt_long_match(arg, name_len, longopts);
        if (idx < 0) {
            optind++;
            return '?';
        }

        if (longindex) {
            *longindex = idx;
        }

        optarg = NULL;
        if (longopts[idx].has_arg == required_argument) {
            if (eq) {
                optarg = (char*)(eq + 1);
            } else if (optind + 1 < argc) {
                optarg = argv[++optind];
            } else {
                optopt = longopts[idx].val;
                optind++;
                return getopt_missing_arg(optstring);
            }
        } else if (longopts[idx].has_arg == optional_argument && eq) {
            optarg = (char*)(eq + 1);
        }

        optind++;
        if (longopts[idx].flag) {
            *longopts[idx].flag = longopts[idx].val;
            return 0;
        }
        return longopts[idx].val;
    }

    return getopt(argc, argv, optstring);
}
#define POSIX_FD_FILE   1
#define POSIX_FD_PIPE   2
#define POSIX_FD_SOCKET 3
#define POSIX_MAX_TRACKED_DIRS 128
#define POSIX_AT_PATH_MAX 512

typedef struct {
    int valid;
    int type;
    int status_flags;
    int fd_flags;
} posix_fd_state_t;

static posix_fd_state_t g_fd_state[POSIX_MAX_TRACKED_FDS];

typedef struct {
    int valid;
    int handle;
    char path[POSIX_AT_PATH_MAX];
} posix_dir_state_t;

static posix_dir_state_t g_dir_state[POSIX_MAX_TRACKED_DIRS];

static void posix_dir_track_open(int handle, const char* path)
{
    if (handle < 0 || !path) {
        return;
    }

    for (size_t i = 0; i < POSIX_MAX_TRACKED_DIRS; i++) {
        if (g_dir_state[i].valid && g_dir_state[i].handle == handle) {
            strlcpy(g_dir_state[i].path, path, sizeof(g_dir_state[i].path));
            return;
        }
    }

    for (size_t i = 0; i < POSIX_MAX_TRACKED_DIRS; i++) {
        if (!g_dir_state[i].valid) {
            g_dir_state[i].valid = 1;
            g_dir_state[i].handle = handle;
            strlcpy(g_dir_state[i].path, path, sizeof(g_dir_state[i].path));
            return;
        }
    }
}

static void posix_dir_track_close(int handle)
{
    for (size_t i = 0; i < POSIX_MAX_TRACKED_DIRS; i++) {
        if (g_dir_state[i].valid && g_dir_state[i].handle == handle) {
            g_dir_state[i].valid = 0;
            g_dir_state[i].handle = -1;
            g_dir_state[i].path[0] = '\0';
            return;
        }
    }
}

static const char* posix_dir_path_from_handle(int handle)
{
    for (size_t i = 0; i < POSIX_MAX_TRACKED_DIRS; i++) {
        if (g_dir_state[i].valid && g_dir_state[i].handle == handle) {
            return g_dir_state[i].path;
        }
    }
    return NULL;
}

static int posix_make_at_path(int dirfd, const char* path, char* out, size_t out_size)
{
    if (!path || !out || out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    if (path[0] == '/' || dirfd == AT_FDCWD) {
        if (strlcpy(out, path, out_size) >= out_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        return 0;
    }

    const char* base = posix_dir_path_from_handle(dirfd);
    if (!base) {
        errno = EBADF;
        return -1;
    }

    size_t base_len = strlen(base);
    size_t path_len = strlen(path);
    int needs_slash = (base_len > 0 && base[base_len - 1] != '/');
    if (base_len + (needs_slash ? 1u : 0u) + path_len + 1u > out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    strlcpy(out, base, out_size);
    if (needs_slash) {
        strlcat(out, "/", out_size);
    }
    strlcat(out, path, out_size);
    return 0;
}

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

static int posix_translate_status(int32_t status)
{
    if (status >= 0) {
        return 0;
    }
    switch (status) {
        case OS_STATUS_NOT_FOUND:      return ENOENT;
        case OS_STATUS_IO_ERROR:       return EIO;
        case OS_STATUS_ACCESS_DENIED:  return EACCES;
        case OS_STATUS_FAULT:          return EFAULT;
        case OS_STATUS_INVALID_ARG:    return EINVAL;
        case OS_STATUS_LIMIT_REACHED:  return EMFILE;
        case OS_STATUS_NOT_SUPPORTED:  return ENOTSUP;
        case OS_STATUS_INTERNAL:       return EIO;
        default:
            if (status <= -4096) {
                return EIO;
            }
            return -status;
    }
}

static void posix_set_errno_from_status(int32_t status)
{
    errno = posix_translate_status(status);
}

static int posix_status_to_rc(int32_t status)
{
    if (status < 0) {
        posix_set_errno_from_status(status);
        return -1;
    }
    return status;
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

int isatty(int fd)
{
    return fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO;
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

int fstatat(int dirfd, const char* path, struct stat* st, int flags)
{
    (void)flags;
    char full_path[POSIX_AT_PATH_MAX];
    if (posix_make_at_path(dirfd, path, full_path, sizeof(full_path)) < 0) {
        return -1;
    }
    return stat(full_path, st);
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
    posix_dir_track_open(handle, path);
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
    posix_dir_track_close(dirp->handle);
    rc = file_closedir(dirp->handle);
    free(dirp);
    return rc;
}

int dirfd(DIR* dirp)
{
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }
    return dirp->handle;
}

int alphasort(const struct dirent** a, const struct dirent** b)
{
    if (!a || !*a || !b || !*b) {
        return 0;
    }
    return strcasecmp((*a)->d_name, (*b)->d_name);
}

int scandir(const char* dirp, struct dirent*** namelist,
            int (*filter)(const struct dirent*),
            int (*compar)(const struct dirent**, const struct dirent**))
{
    if (!dirp || !namelist) {
        errno = EINVAL;
        return -1;
    }

    DIR* dir = opendir(dirp);
    if (!dir) {
        return -1;
    }

    struct dirent** list = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent* ent;

    while ((ent = readdir(dir)) != NULL) {
        if (filter && !filter(ent)) {
            continue;
        }

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            struct dirent** new_list =
                (struct dirent**)realloc(list, new_capacity * sizeof(*new_list));
            if (!new_list) {
                for (size_t i = 0; i < count; i++) {
                    free(list[i]);
                }
                free(list);
                closedir(dir);
                errno = ENOMEM;
                return -1;
            }
            list = new_list;
            capacity = new_capacity;
        }

        struct dirent* copy = (struct dirent*)malloc(sizeof(*copy));
        if (!copy) {
            for (size_t i = 0; i < count; i++) {
                free(list[i]);
            }
            free(list);
            closedir(dir);
            errno = ENOMEM;
            return -1;
        }
        memcpy(copy, ent, sizeof(*copy));
        list[count++] = copy;
    }

    closedir(dir);

    if (compar) {
        for (size_t i = 1; i < count; i++) {
            struct dirent* item = list[i];
            size_t j = i;
            const struct dirent* lhs = item;
            const struct dirent* rhs = NULL;
            while (j > 0 &&
                   ((rhs = list[j - 1]),
                    compar(&lhs, &rhs) < 0)) {
                list[j] = list[j - 1];
                j--;
            }
            list[j] = item;
        }
    }

    *namelist = list;
    return (int)count;
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

pid_t fork(void)
{
    int64_t child_pid = (int64_t)syscall0(SYSCALL_FORK);
    if (child_pid < 0) {
        errno = (int)-child_pid;
        return -1;
    }
    return (pid_t)child_pid;
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
                (ready & (POLLOUT | POLLERR | POLLHUP)) != 0u) {
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
    int fd_flags = 0;
    int status_flags = 0;
    int socket_type = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if ((type & SOCK_NONBLOCK) != 0) {
        status_flags |= O_NONBLOCK;
    }
    if ((type & SOCK_CLOEXEC) != 0) {
        fd_flags |= FD_CLOEXEC;
    }
    if (socket_type != SOCK_STREAM ||
        (protocol != 0 && protocol != IPPROTO_TCP)) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    int fd = socket_create(socket_type);
    if (fd >= 0) {
        posix_fd_mark_open(fd, status_flags);
        posix_fd_state(fd)->type = POSIX_FD_SOCKET;
        posix_fd_state(fd)->fd_flags = fd_flags;
    } else {
        posix_set_errno_from_status(fd);
        return -1;
    }
    return fd;
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
    const struct sockaddr_in* in = (const struct sockaddr_in*)addr;
    posix_fd_state_t* state = posix_fd_state(sockfd);
    if (!addr || addrlen < sizeof(*in) || in->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    if (!state || !state->valid || state->type != POSIX_FD_SOCKET) {
        errno = EBADF;
        return -1;
    }

    socket_info_t info;
    if (socket_get_info(sockfd, &info) == 0) {
        if (info.state == 4u) {
            errno = EISCONN;
            return -1;
        }
        if (info.state == 2u || info.state == 3u) {
            if ((state->status_flags & O_NONBLOCK) != 0) {
                errno = EALREADY;
                return -1;
            }
            uint64_t existing_deadline = get_uptime_ms() + 10000u;
            for (;;) {
                if (socket_get_info(sockfd, &info) < 0) {
                    errno = EIO;
                    return -1;
                }
                if (info.state == 4u) return 0;
                if (info.error != 0) {
                    errno = info.error;
                    return -1;
                }
                if (info.state == 0u || get_uptime_ms() >= existing_deadline) {
                    errno = ETIMEDOUT;
                    return -1;
                }
                sleep_ms(1u);
            }
        }
    }

    int result = socket_connect(
        sockfd, ntohl(in->sin_addr.s_addr), ntohs(in->sin_port));
    if (result < 0) {
        posix_set_errno_from_status(result);
        return -1;
    }

    if ((state->status_flags & O_NONBLOCK) != 0) {
        if (socket_get_info(sockfd, &info) == 0 && info.state == 4u) {
            return 0;
        }
        errno = EINPROGRESS;
        return -1;
    }

    uint64_t deadline = get_uptime_ms() + 10000u;
    for (;;) {
        if (socket_get_info(sockfd, &info) < 0) {
            errno = EIO;
            return -1;
        }
        if (info.state == 4u) return 0;
        if (info.error != 0) {
            errno = info.error;
            return -1;
        }
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
    return posix_status_to_rc(socket_bind(sockfd, ntohs(in->sin_port)));
}

int listen(int sockfd, int backlog)
{
    if (backlog < 0) {
        errno = EINVAL;
        return -1;
    }
    return posix_status_to_rc(socket_listen_with_backlog(sockfd, backlog));
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
    posix_fd_state_t* state = posix_fd_state(sockfd);
    if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!buf && len != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (!state || !state->valid || state->type != POSIX_FD_SOCKET) {
        errno = EBADF;
        return -1;
    }
    for (;;) {
        int32_t result = socket_send(sockfd, buf, (uint32_t)len);
        if (result < 0) {
            posix_set_errno_from_status(result);
            return -1;
        }
        if (result > 0 || len == 0u) {
            return (ssize_t)result;
        }
        if ((flags & MSG_DONTWAIT) != 0 ||
            (state->status_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
        }
        socket_info_t info;
        if (socket_get_info(sockfd, &info) < 0 || info.state != 4u) {
            errno = EPIPE;
            return -1;
        }
        sleep_ms(1u);
    }
}

ssize_t recv(int sockfd, void* buf, size_t len, int flags)
{
    posix_fd_state_t* state = posix_fd_state(sockfd);
    if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!buf && len != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (!state || !state->valid || state->type != POSIX_FD_SOCKET) {
        errno = EBADF;
        return -1;
    }
    for (;;) {
        int32_t result = socket_recv(sockfd, buf, (uint32_t)len);
        if (result < 0) {
            posix_set_errno_from_status(result);
            return -1;
        }
        if (result != 0 || len == 0u) {
            return (ssize_t)result;
        }
        socket_info_t info;
        if (socket_get_info(sockfd, &info) < 0 ||
            (info.state != 4u && info.state != 5u && info.state != 6u)) {
            return 0;
        }
        if ((flags & MSG_DONTWAIT) != 0 ||
            (state->status_flags & O_NONBLOCK) != 0) {
            errno = EAGAIN;
            return -1;
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
    return posix_status_to_rc(socket_shutdown(sockfd, how));
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

#define LIBC_MAX_THREADS 256
#define LIBC_TLS_MAX_KEYS 256
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
    if (thread == 0) {
        return EINVAL;
    }
    libc_thread_lock();
    libc_thread_desc_t *desc = libc_thread_find_locked(thread);
    if (desc == NULL) {
        libc_thread_unlock();
        return ESRCH;
    }
    desc->done = 1;
    __sync_synchronize();
    libc_futex_wake(&desc->done, 0x7fffffff);
    int cleanup = desc->detached && desc->creator_ready;
    libc_thread_unlock();
    (void)syscall2(SYSCALL_TKILL, (uint64_t)thread, (uint64_t)SIGTERM);
    if (cleanup) {
        libc_thread_cleanup(desc);
    }
    return 0;
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

int execve(const char *path, char *const argv[], char *const envp[])
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int64_t r = (int64_t)syscall3(SYSCALL_EXECVE,
                                   (uint64_t)(uintptr_t)path,
                                   (uint64_t)(uintptr_t)argv,
                                   (uint64_t)(uintptr_t)envp);
    if (r < 0) {
        errno = (int)-r;
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

ssize_t getrandom(void *buffer, size_t length, unsigned int flags)
{
    if (!buffer && length != 0u) {
        errno = EINVAL;
        return -1;
    }
    int64_t result = (int64_t)syscall3(SYSCALL_GETRANDOM,
                                       (uint64_t)(uintptr_t)buffer,
                                       (uint64_t)length,
                                       (uint64_t)flags);
    if (result < 0) {
        posix_set_errno_from_status((int32_t)result);
        return -1;
    }
    return (ssize_t)result;
}

int getentropy(void *buffer, size_t length)
{
    if (length > 256u) {
        errno = EIO;
        return -1;
    }
    ssize_t result = getrandom(buffer, length, 0u);
    if (result < 0) {
        return -1;
    }
    if ((size_t)result != length) {
        errno = EIO;
        return -1;
    }
    return 0;
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

struct tm *gmtime(const time_t *timep)
{
    static struct tm result;
    return gmtime_r(timep, &result);
}

struct tm *localtime(const time_t *timep)
{
    static struct tm result;
    return localtime_r(timep, &result);
}

static int strftime_append_char(char* s, size_t max, size_t* pos, char c)
{
    if (*pos + 1 >= max) {
        return 0;
    }
    s[*pos] = c;
    (*pos)++;
    s[*pos] = '\0';
    return 1;
}

static int strftime_append_string(char* s, size_t max, size_t* pos, const char* value)
{
    while (value && *value) {
        if (!strftime_append_char(s, max, pos, *value++)) {
            return 0;
        }
    }
    return 1;
}

static int strftime_append_number(char* s, size_t max, size_t* pos,
                                  int value, int width, char pad)
{
    char digits[16];
    int ndigits = 0;

    if (value < 0) {
        value = 0;
    }

    do {
        digits[ndigits++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && ndigits < (int)sizeof(digits));

    while (ndigits < width) {
        if (!strftime_append_char(s, max, pos, pad)) {
            return 0;
        }
        width--;
    }

    while (ndigits > 0) {
        if (!strftime_append_char(s, max, pos, digits[--ndigits])) {
            return 0;
        }
    }
    return 1;
}

size_t strftime(char* s, size_t max, const char* format, const struct tm* tm)
{
    static const char* const wday_short[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char* const wday_long[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    static const char* const mon_short[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char* const mon_long[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };

    if (!s || max == 0 || !format || !tm) {
        return 0;
    }

    s[0] = '\0';
    size_t pos = 0;

    for (const char* p = format; *p; p++) {
        if (*p != '%') {
            if (!strftime_append_char(s, max, &pos, *p)) {
                return 0;
            }
            continue;
        }

        p++;
        if (*p == '\0') {
            if (!strftime_append_char(s, max, &pos, '%')) {
                return 0;
            }
            break;
        }

        int mon = tm->tm_mon;
        int wday = tm->tm_wday;
        if (mon < 0 || mon > 11) mon = 0;
        if (wday < 0 || wday > 6) wday = 0;

        switch (*p) {
            case '%':
                if (!strftime_append_char(s, max, &pos, '%')) return 0;
                break;
            case 'a':
                if (!strftime_append_string(s, max, &pos, wday_short[wday])) return 0;
                break;
            case 'A':
                if (!strftime_append_string(s, max, &pos, wday_long[wday])) return 0;
                break;
            case 'b':
            case 'h':
                if (!strftime_append_string(s, max, &pos, mon_short[mon])) return 0;
                break;
            case 'B':
                if (!strftime_append_string(s, max, &pos, mon_long[mon])) return 0;
                break;
            case 'd':
                if (!strftime_append_number(s, max, &pos, tm->tm_mday, 2, '0')) return 0;
                break;
            case 'e':
                if (!strftime_append_number(s, max, &pos, tm->tm_mday, 2, ' ')) return 0;
                break;
            case 'H':
                if (!strftime_append_number(s, max, &pos, tm->tm_hour, 2, '0')) return 0;
                break;
            case 'M':
                if (!strftime_append_number(s, max, &pos, tm->tm_min, 2, '0')) return 0;
                break;
            case 'S':
                if (!strftime_append_number(s, max, &pos, tm->tm_sec, 2, '0')) return 0;
                break;
            case 'm':
                if (!strftime_append_number(s, max, &pos, tm->tm_mon + 1, 2, '0')) return 0;
                break;
            case 'Y':
                if (!strftime_append_number(s, max, &pos, tm->tm_year + 1900, 4, '0')) return 0;
                break;
            case 'y':
                if (!strftime_append_number(s, max, &pos, (tm->tm_year + 1900) % 100, 2, '0')) return 0;
                break;
            case 'F':
                if (!strftime_append_number(s, max, &pos, tm->tm_year + 1900, 4, '0')) return 0;
                if (!strftime_append_char(s, max, &pos, '-')) return 0;
                if (!strftime_append_number(s, max, &pos, tm->tm_mon + 1, 2, '0')) return 0;
                if (!strftime_append_char(s, max, &pos, '-')) return 0;
                if (!strftime_append_number(s, max, &pos, tm->tm_mday, 2, '0')) return 0;
                break;
            case 'R':
                if (!strftime_append_number(s, max, &pos, tm->tm_hour, 2, '0')) return 0;
                if (!strftime_append_char(s, max, &pos, ':')) return 0;
                if (!strftime_append_number(s, max, &pos, tm->tm_min, 2, '0')) return 0;
                break;
            case 'T':
                if (!strftime_append_number(s, max, &pos, tm->tm_hour, 2, '0')) return 0;
                if (!strftime_append_char(s, max, &pos, ':')) return 0;
                if (!strftime_append_number(s, max, &pos, tm->tm_min, 2, '0')) return 0;
                if (!strftime_append_char(s, max, &pos, ':')) return 0;
                if (!strftime_append_number(s, max, &pos, tm->tm_sec, 2, '0')) return 0;
                break;
            default:
                if (!strftime_append_char(s, max, &pos, '%')) return 0;
                if (!strftime_append_char(s, max, &pos, *p)) return 0;
                break;
        }
    }

    return pos;
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

char *getcwd(char *buf, size_t size)
{
    int64_t result = (int64_t)syscall2(SYSCALL_GETCWD, (uint64_t)(uintptr_t)buf, (uint64_t)size);
    if (result < 0) {
        errno = (int)-result;
        return NULL;
    }
    return buf;
}

int chdir(const char *path)
{
    (void)path;
    errno = ENOSYS;
    return -1;
}

int chroot(const char *path)
{
    (void)path;
    errno = ENOSYS;
    return -1;
}

int access(const char *path, int mode)
{
    (void)mode;
    os_file_stat_t info;
    if (file_stat(path, &info) < 0 || !info.exists) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

char *realpath(const char *path, char *resolved_path)
{
    char temp[POSIX_AT_PATH_MAX];
    char cwd_buf[POSIX_AT_PATH_MAX];
    char *out;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    if (path[0] == '/') {
        if (strlcpy(temp, path, sizeof(temp)) >= sizeof(temp)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
    } else {
        if (!getcwd(cwd_buf, sizeof(cwd_buf))) {
            return NULL;
        }
        size_t cwd_len = strlen(cwd_buf);
        size_t path_len = strlen(path);
        int needs_slash = (cwd_len > 0 && cwd_buf[cwd_len - 1] != '/');
        if (cwd_len + (needs_slash ? 1u : 0u) + path_len + 1u > sizeof(temp)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        strlcpy(temp, cwd_buf, sizeof(temp));
        if (needs_slash) {
            strlcat(temp, "/", sizeof(temp));
        }
        strlcat(temp, path, sizeof(temp));
    }

    if (access(temp, F_OK) != 0) {
        return NULL;
    }

    out = resolved_path;
    if (!out) {
        out = (char*)malloc(strlen(temp) + 1u);
        if (!out) {
            errno = ENOMEM;
            return NULL;
        }
    }
    strcpy(out, temp);
    return out;
}

int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

int symlink(const char *target, const char *linkpath)
{
    (void)target;
    (void)linkpath;
    errno = ENOSYS;
    return -1;
}

ssize_t readlink(const char *path, char *buf, size_t bufsize)
{
    (void)path;
    (void)buf;
    (void)bufsize;
    errno = ENOSYS;
    return -1;
}

int link(const char *oldpath, const char *newpath)
{
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

int truncate(const char *path, off_t length)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int rc = ftruncate(fd, length);
    close(fd);
    return rc;
}

int ftruncate(int fd, off_t length)
{
    off_t current = lseek(fd, 0, SEEK_CUR);
    if (current < 0) return -1;
    if (length < current) {
        (void)lseek(fd, length, SEEK_SET);
    }
    (void)lseek(fd, current, SEEK_SET);
    return 0;
}

int chmod(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int fchmod(int fd, mode_t mode)
{
    (void)fd;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int chown(const char *path, uid_t owner, gid_t group)
{
    (void)path;
    (void)owner;
    (void)group;
    errno = ENOSYS;
    return -1;
}

int fchown(int fd, uid_t owner, gid_t group)
{
    (void)fd;
    (void)owner;
    (void)group;
    errno = ENOSYS;
    return -1;
}

uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }

int openat(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    char full_path[POSIX_AT_PATH_MAX];
    if (posix_make_at_path(dirfd, path, full_path, sizeof(full_path)) < 0) {
        return -1;
    }

    va_start(ap, flags);
    int rc;
    if (flags & O_CREAT) {
        (void)va_arg(ap, mode_t);
        rc = open(full_path, flags | O_CREAT);
    } else {
        rc = open(full_path, flags);
    }
    va_end(ap);
    return rc;
}

int unlinkat(int dirfd, const char *path, int flags)
{
    char full_path[POSIX_AT_PATH_MAX];
    if (posix_make_at_path(dirfd, path, full_path, sizeof(full_path)) < 0) {
        return -1;
    }
    if (flags & AT_REMOVEDIR) {
        return rmdir(full_path);
    }
    return unlink(full_path);
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    char full_path[POSIX_AT_PATH_MAX];
    if (posix_make_at_path(dirfd, path, full_path, sizeof(full_path)) < 0) {
        return -1;
    }
    return mkdir(full_path, mode);
}

int rmdir(const char *path)
{
    return file_unlink(path);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) {
            if (total > 0) break;
            return -1;
        }
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) {
            if (total > 0) break;
            return -1;
        }
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0) return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = read(fd, buf, count);
    off_t rc = lseek(fd, saved, SEEK_SET);
    (void)rc;
    return n;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0) return -1;
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    ssize_t n = write(fd, buf, count);
    off_t rc = lseek(fd, saved, SEEK_SET);
    (void)rc;
    return n;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (dest_addr && addrlen >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in*)dest_addr;
        int result = socket_connect(sockfd, ntohl(in->sin_addr.s_addr), ntohs(in->sin_port));
        if (result < 0) return result;
    }
    return send(sockfd, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    ssize_t n = recv(sockfd, buf, len, flags);
    if (n >= 0 && src_addr && addrlen) {
        socket_info_t info;
        if (socket_get_info(sockfd, &info) == 0) {
            struct sockaddr_in in;
            memset(&in, 0, sizeof(in));
            in.sin_family = AF_INET;
            in.sin_addr.s_addr = htonl(info.remote_ip);
            in.sin_port = htons(info.remote_port);
            socklen_t copy = sizeof(in);
            if (*addrlen < copy) copy = *addrlen;
            memcpy(src_addr, &in, copy);
            *addrlen = copy;
        }
    }
    return n;
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    if (domain != AF_UNIX && domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (!sv) {
        errno = EINVAL;
        return -1;
    }
    return pipe(sv);
}

char *inet_ntoa(struct in_addr in)
{
    static char buf[16];
    uint32_t addr = ntohl(in.s_addr);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (unsigned)((addr >> 24) & 0xFF),
             (unsigned)((addr >> 16) & 0xFF),
             (unsigned)((addr >> 8) & 0xFF),
             (unsigned)(addr & 0xFF));
    return buf;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    if (!src || !dst || size < 16) {
        errno = ENOSPC;
        return NULL;
    }
    uint32_t addr = ntohl(*(const uint32_t*)src);
    snprintf(dst, (size_t)size, "%u.%u.%u.%u",
             (unsigned)((addr >> 24) & 0xFF),
             (unsigned)((addr >> 16) & 0xFF),
             (unsigned)((addr >> 8) & 0xFF),
             (unsigned)(addr & 0xFF));
    return dst;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (!src || !dst) {
        errno = EINVAL;
        return 0;
    }
    struct in_addr addr;
    if (!inet_aton(src, &addr)) return 0;
    *(struct in_addr*)dst = addr;
    return 1;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res)
{
    const int supported_flags = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
                                AI_NUMERICSERV | AI_ADDRCONFIG;
    uint16_t port = 0u;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    int flags = hints ? hints->ai_flags : 0;

    if (!res) return EAI_BADFLAGS;
    *res = NULL;
    if (!node && !service) return EAI_NONAME;
    if ((flags & ~supported_flags) != 0) return EAI_BADFLAGS;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    if (socktype == 0) socktype = SOCK_STREAM;
    if (protocol == 0) {
        protocol = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
    } else if ((socktype == SOCK_STREAM && protocol != IPPROTO_TCP) ||
               (socktype == SOCK_DGRAM && protocol != IPPROTO_UDP)) {
        return EAI_SERVICE;
    }

    if (service) {
        char *end = NULL;
        long parsed = strtol(service, &end, 10);
        if (end != service && *end == '\0' && parsed >= 0 && parsed <= 65535) {
            port = (uint16_t)parsed;
        } else if ((flags & AI_NUMERICSERV) != 0) {
            return EAI_SERVICE;
        } else if (strcmp(service, "http") == 0) {
            port = 80u;
        } else if (strcmp(service, "https") == 0) {
            port = 443u;
        } else if (strcmp(service, "domain") == 0) {
            port = 53u;
        } else {
            return EAI_SERVICE;
        }
    }

    struct addrinfo *ai = (struct addrinfo*)calloc(1, sizeof(struct addrinfo));
    if (!ai) return EAI_MEMORY;

    struct sockaddr_in *sin = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
    if (!sin) { free(ai); return EAI_MEMORY; }

    ai->ai_flags = flags;
    ai->ai_family = AF_INET;
    ai->ai_socktype = socktype;
    ai->ai_protocol = protocol;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr = (struct sockaddr*)sin;

    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = INADDR_ANY;

    if (node) {
        if (inet_pton(AF_INET, node, &sin->sin_addr) <= 0) {
            if ((flags & AI_NUMERICHOST) != 0) {
                free(sin);
                free(ai);
                return EAI_NONAME;
            }
            uint32_t resolved = dns_resolve(node);
            if (resolved == 0u) {
                free(sin);
                free(ai);
                return EAI_NONAME;
            }
            sin->sin_addr.s_addr = htonl(resolved);
        }
        if ((flags & AI_CANONNAME) != 0) {
            ai->ai_canonname = strdup(node);
            if (!ai->ai_canonname) {
                free(sin);
                free(ai);
                return EAI_MEMORY;
            }
        }
    } else if (ai->ai_flags & AI_PASSIVE) {
        sin->sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_addr);
        free(res->ai_canonname);
        free(res);
        res = next;
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                char *host, size_t hostlen, char *serv, size_t servlen,
                int flags)
{
    if (!sa || salen < sizeof(struct sockaddr_in)) return EAI_FAMILY;
    const struct sockaddr_in *sin = (const struct sockaddr_in*)sa;

    if (host && hostlen > 0) {
        if (flags & NI_NUMERICHOST) {
            if (!inet_ntop(AF_INET, &sin->sin_addr, host, (socklen_t)hostlen))
                return EAI_SYSTEM;
        } else {
            if (!inet_ntop(AF_INET, &sin->sin_addr, host, (socklen_t)hostlen))
                return EAI_SYSTEM;
        }
    }
    if (serv && servlen > 0) {
        snprintf(serv, servlen, "%u", (unsigned)ntohs(sin->sin_port));
    }
    return 0;
}

const char *gai_strerror(int errcode)
{
    switch (errcode) {
        case 0: return "Success";
        case EAI_BADFLAGS: return "Bad flags";
        case EAI_NONAME: return "Name or service not known";
        case EAI_AGAIN: return "Temporary name resolution failure";
        case EAI_FAIL: return "Non-recoverable name resolution failure";
        case EAI_FAMILY: return "Address family not supported";
        case EAI_MEMORY: return "Memory allocation failure";
        case EAI_SERVICE: return "Service not supported";
        case EAI_SOCKTYPE: return "Socket type not supported";
        case EAI_NODATA: return "No address associated with name";
        case EAI_SYSTEM: return "System error";
        default: return "Unknown getaddrinfo error";
    }
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent he;
    static char *alias_list[2];
    static char *addr_list[2];
    static struct in_addr addr;
    static char hostname_buf[256];

    if (!name) return NULL;
    strncpy(hostname_buf, name, sizeof(hostname_buf) - 1);
    hostname_buf[sizeof(hostname_buf) - 1] = '\0';

    if (inet_pton(AF_INET, name, &addr) <= 0) {
        uint32_t resolved = dns_resolve(name);
        if (resolved == 0u) {
            errno = ENOENT;
            return NULL;
        }
        addr.s_addr = htonl(resolved);
    }

    he.h_name = hostname_buf;
    alias_list[0] = NULL;
    he.h_aliases = alias_list;
    he.h_addrtype = AF_INET;
    he.h_length = sizeof(struct in_addr);
    addr_list[0] = (char*)&addr;
    addr_list[1] = NULL;
    he.h_addr_list = addr_list;
    return &he;
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type)
{
    static struct hostent he;
    static char *alias_list[2];
    static char *addr_list[2];
    static struct in_addr local_addr;
    static char hostname_buf[64];

    if (type != AF_INET || !addr || len < sizeof(struct in_addr)) return NULL;

    local_addr = *(const struct in_addr*)addr;
    inet_ntop(AF_INET, &local_addr, hostname_buf, sizeof(hostname_buf));

    he.h_name = hostname_buf;
    alias_list[0] = NULL;
    he.h_aliases = alias_list;
    he.h_addrtype = AF_INET;
    he.h_length = sizeof(struct in_addr);
    addr_list[0] = (char*)&local_addr;
    addr_list[1] = NULL;
    he.h_addr_list = addr_list;
    return &he;
}

struct servent *getservbyname(const char *name, const char *proto)
{
    static struct servent service;
    static char *aliases[1];
    if (!name) {
        errno = EINVAL;
        return NULL;
    }
    if (proto && strcmp(proto, "tcp") != 0 && strcmp(proto, "udp") != 0) {
        errno = ENOENT;
        return NULL;
    }
    memset(&service, 0, sizeof(service));
    aliases[0] = NULL;
    service.s_name = (char*)name;
    service.s_aliases = aliases;
    service.s_proto = (char*)(proto ? proto : "tcp");
    if (strcmp(name, "http") == 0) {
        service.s_port = (int)htons(80u);
    } else if (strcmp(name, "https") == 0) {
        service.s_port = (int)htons(443u);
    } else if (strcmp(name, "domain") == 0) {
        service.s_port = (int)htons(53u);
    } else {
        errno = ENOENT;
        return NULL;
    }
    return &service;
}

struct protoent *getprotobyname(const char *name)
{
    static struct protoent proto;
    static char *aliases[1];
    if (!name) {
        errno = EINVAL;
        return NULL;
    }
    memset(&proto, 0, sizeof(proto));
    aliases[0] = NULL;
    proto.p_name = (char*)name;
    proto.p_aliases = aliases;
    if (strcmp(name, "tcp") == 0) {
        proto.p_proto = IPPROTO_TCP;
    } else if (strcmp(name, "udp") == 0) {
        proto.p_proto = IPPROTO_UDP;
    } else {
        errno = ENOENT;
        return NULL;
    }
    return &proto;
}

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    if (!rwlock) return EINVAL;
    (void)attr;
    rwlock->locked = 0;
    rwlock->readers = 0;
    rwlock->writer = 0;
    rwlock->waiter_count = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    for (;;) {
        while (rwlock->locked) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
        if (__sync_bool_compare_and_swap(&rwlock->readers, 0, 1)) {
            break;
        }
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    if (rwlock->locked) return EBUSY;
    __sync_fetch_and_add(&rwlock->readers, 1);
    if (rwlock->locked) {
        __sync_fetch_and_sub(&rwlock->readers, 1);
        return EBUSY;
    }
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    pthread_t self = pthread_self();
    for (;;) {
        while (rwlock->locked || rwlock->readers > 0) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
        if (__sync_bool_compare_and_swap(&rwlock->locked, 0, 1)) {
            rwlock->writer = self;
            break;
        }
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    if (rwlock->locked || rwlock->readers > 0) return EBUSY;
    if (!__sync_bool_compare_and_swap(&rwlock->locked, 0, 1)) return EBUSY;
    rwlock->writer = pthread_self();
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    if (!rwlock) return EINVAL;
    if (rwlock->locked) {
        rwlock->writer = 0;
        __sync_lock_release(&rwlock->locked);
    } else if (rwlock->readers > 0) {
        __sync_fetch_and_sub(&rwlock->readers, 1);
    }
    return 0;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
    if (!attr) return EINVAL;
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned count)
{
    if (!barrier || count == 0) return EINVAL;
    (void)attr;
    barrier->count = 0;
    barrier->sense = 0;
    barrier->threshold = (int)count;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    if (!barrier) return EINVAL;
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    if (!barrier) return EINVAL;
    int sense = barrier->sense;
    int arrived = __sync_add_and_fetch(&barrier->count, 1);
    if (arrived == barrier->threshold) {
        __sync_synchronize();
        barrier->count = 0;
        barrier->sense = !sense;
        return PTHREAD_BARRIER_SERIAL_THREAD;
    } else {
        while (barrier->sense == sense) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
    }
    return 0;
}

int pthread_barrierattr_init(pthread_barrierattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attr)
{
    if (!attr) return EINVAL;
    return 0;
}

int pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
    if (!lock) return EINVAL;
    (void)pshared;
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
    if (!lock) return EINVAL;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
    if (!lock) return EINVAL;
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) {
            __asm__ __volatile__ ("pause" ::: "memory");
        }
    }
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
    if (!lock) return EINVAL;
    return __sync_lock_test_and_set(lock, 1) ? EBUSY : 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
    if (!lock) return EINVAL;
    __sync_lock_release(lock);
    return 0;
}

int pthread_attr_init(pthread_attr_t *attr)
{
    if (!attr) return EINVAL;
    attr->detached = 0;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    if (!attr) return EINVAL;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
    if (!attr) return EINVAL;
    attr->detached = (detachstate == PTHREAD_CREATE_DETACHED) ? 1 : 0;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    if (!attr || !detachstate) return EINVAL;
    *detachstate = attr->detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    if (!attr) return EINVAL;
    (void)stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    if (!attr || !stacksize) return EINVAL;
    *stacksize = 0x100000;
    return 0;
}

int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr)
{
    if (!attr) return EINVAL;
    (void)stackaddr;
    return 0;
}

int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr)
{
    if (!attr || !stackaddr) return EINVAL;
    *stackaddr = NULL;
    return 0;
}

static struct { void (*prepare)(void); void (*parent)(void); void (*child)(void); } g_atfork_handlers;

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    g_atfork_handlers.prepare = prepare;
    g_atfork_handlers.parent = parent;
    g_atfork_handlers.child = child;
    return 0;
}

/* ---- Semaphore ---- */

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    if (!sem) return -1;
    (void)pshared;
    sem->count = (int)value;
    sem->lock = 0;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    if (!sem) return -1;
    return 0;
}

int sem_post(sem_t *sem)
{
    if (!sem) return -1;
    __sync_fetch_and_add(&sem->count, 1);
    return 0;
}

int sem_wait(sem_t *sem)
{
    if (!sem) return -1;
    for (;;) {
        int val = sem->count;
        if (val > 0) {
            if (__sync_bool_compare_and_swap(&sem->count, val, val - 1)) {
                return 0;
            }
        }
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

int sem_trywait(sem_t *sem)
{
    if (!sem) return -1;
    int val = sem->count;
    if (val <= 0) {
        errno = EAGAIN;
        return -1;
    }
    if (__sync_bool_compare_and_swap(&sem->count, val, val - 1)) return 0;
    errno = EAGAIN;
    return -1;
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
    if (!sem || !abs_timeout) return -1;
    uint64_t deadline = (uint64_t)abs_timeout->tv_sec * 1000ULL +
                        (uint64_t)(abs_timeout->tv_nsec / 1000000L);
    for (;;) {
        int val = sem->count;
        if (val > 0) {
            if (__sync_bool_compare_and_swap(&sem->count, val, val - 1)) return 0;
        }
        if (get_uptime_ms() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

int sem_getvalue(sem_t *sem, int *sval)
{
    if (!sem || !sval) return -1;
    *sval = sem->count;
    return 0;
}


/* ---- uname ---- */

int uname(struct utsname *buf)
{
    if (!buf) { errno = EINVAL; return -1; }
    strncpy(buf->sysname, "ImplusOS", SYS_NMLN);
    strncpy(buf->nodename, "localhost", SYS_NMLN);
    strncpy(buf->release, "0.1.0", SYS_NMLN);
    strncpy(buf->version, "#1", SYS_NMLN);
    strncpy(buf->machine, "x86_64", SYS_NMLN);
    return 0;
}

/* ---- resource limits ---- */

int getrlimit(int resource, struct rlimit *rlim)
{
    if (!rlim) { errno = EINVAL; return -1; }
    (void)resource;
    rlim->rlim_cur = RLIM_INFINITY;
    rlim->rlim_max = RLIM_INFINITY;
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
    if (!rlim) { errno = EINVAL; return -1; }
    (void)resource;
    return 0;
}

int getrusage(int who, struct rusage *usage)
{
    if (!usage) { errno = EINVAL; return -1; }
    (void)who;
    memset(usage, 0, sizeof(*usage));
    return 0;
}

/* ---- locale ---- */

static char g_locale_buf[] = "C";
static struct lconv g_locale = {0};
static int g_locale_init_done = 0;

static void locale_init(void)
{
    if (g_locale_init_done) return;
    g_locale.decimal_point = ".";
    g_locale.thousands_sep = "";
    g_locale.grouping = "";
    g_locale.int_curr_symbol = "";
    g_locale.currency_symbol = "";
    g_locale.mon_decimal_point = "";
    g_locale.mon_thousands_sep = "";
    g_locale.mon_grouping = "";
    g_locale.positive_sign = "";
    g_locale.negative_sign = "";
    g_locale.int_frac_digits = 0;
    g_locale.frac_digits = 0;
    g_locale.p_cs_precedes = 0;
    g_locale.p_sep_by_space = 0;
    g_locale.n_cs_precedes = 0;
    g_locale.n_sep_by_space = 0;
    g_locale.p_sign_posn = 0;
    g_locale.n_sign_posn = 0;
    g_locale.int_p_cs_precedes = 0;
    g_locale.int_p_sep_by_space = 0;
    g_locale.int_n_cs_precedes = 0;
    g_locale.int_n_sep_by_space = 0;
    g_locale.int_p_sign_posn = 0;
    g_locale.int_n_sign_posn = 0;
    g_locale_init_done = 1;
}

char *setlocale(int category, const char *locale)
{
    (void)category;
    if (!locale) return g_locale_buf;
    if (strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0) return g_locale_buf;
    if (strcmp(locale, "") == 0) return g_locale_buf;
    return NULL;
}

struct lconv *localeconv(void)
{
    locale_init();
    return &g_locale;
}

/* ---- wchar / multibyte ---- */

size_t mbstowcs(wchar_t *dst, const char *src, size_t len)
{
    if (!src) return 0;
    size_t i = 0;
    while (*src && i < len) {
        if (dst) dst[i] = (wchar_t)(unsigned char)*src;
        src++;
        i++;
    }
    if (dst && i < len) dst[i] = 0;
    return i;
}

size_t wcstombs(char *dst, const wchar_t *src, size_t len)
{
    if (!src) return 0;
    size_t i = 0;
    while (*src && i < len) {
        wchar_t wc = *src;
        if (wc > 0x7F) {
            if (dst) dst[i] = '?';
        } else {
            if (dst) dst[i] = (char)wc;
        }
        src++;
        i++;
    }
    if (dst && i < len) dst[i] = '\0';
    return i;
}

int mbtowc(wchar_t *pwc, const char *s, size_t n)
{
    if (!s) return 0;
    if (n == 0) return -1;
    if (pwc) *pwc = (wchar_t)(unsigned char)*s;
    return (*s != '\0') ? 1 : 0;
}

int wctomb(char *s, wchar_t wchar)
{
    if (!s) return 0;
    if (wchar > 0x7F) return -1;
    *s = (char)wchar;
    return 1;
}

/* ---- wchar string functions ---- */

size_t wcslen(const wchar_t *s)
{
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while ((*d++ = *src++));
    return dst;
}

int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(*a - *b);
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(a[i] - b[i]);
        if (a[i] == 0) return 0;
    }
    return 0;
}

wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    while (*s) {
        if (*s == c) return (wchar_t*)s;
        s++;
    }
    return (c == 0) ? (wchar_t*)s : NULL;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    return (c == 0) ? (wchar_t*)s : (wchar_t*)last;
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *accept)
{
    while (*s) {
        for (const wchar_t *a = accept; *a; a++) {
            if (*s == *a) return (wchar_t*)s;
        }
        s++;
    }
    return NULL;
}

size_t wcsspn(const wchar_t *s, const wchar_t *accept)
{
    const wchar_t *p = s;
    while (*p) {
        int found = 0;
        for (const wchar_t *a = accept; *a; a++) {
            if (*p == *a) { found = 1; break; }
        }
        if (!found) break;
        p++;
    }
    return (size_t)(p - s);
}

size_t wcscspn(const wchar_t *s, const wchar_t *reject)
{
    const wchar_t *p = s;
    while (*p) {
        for (const wchar_t *r = reject; *r; r++) {
            if (*p == *r) return (size_t)(p - s);
        }
        p++;
    }
    return (size_t)(p - s);
}

wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)
{
    if (!*needle) return (wchar_t*)haystack;
    size_t nlen = wcslen(needle);
    while (*haystack) {
        if (*haystack == *needle && wcsncmp(haystack, needle, nlen) == 0)
            return (wchar_t*)haystack;
        haystack++;
    }
    return NULL;
}

wchar_t *wcstok(wchar_t *str, const wchar_t *delim, wchar_t **saveptr)
{
    if (!saveptr) return NULL;
    if (str != NULL) *saveptr = str;
    if (*saveptr == NULL || **saveptr == L'\0') return NULL;

    wchar_t *p = *saveptr;
    while (*p) {
        int is_delim = 0;
        for (const wchar_t *d = delim; *d; d++) {
            if (*p == *d) { is_delim = 1; break; }
        }
        if (!is_delim) break;
        p++;
    }
    if (*p == L'\0') { *saveptr = NULL; return NULL; }

    wchar_t *token = p;
    while (*p) {
        int is_delim = 0;
        for (const wchar_t *d = delim; *d; d++) {
            if (*p == *d) { is_delim = 1; break; }
        }
        if (is_delim) {
            *p = L'\0';
            *saveptr = p + 1;
            return token;
        }
        p++;
    }
    *saveptr = NULL;
    return token;
}

wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst + wcslen(dst);
    size_t i = 0;
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    *d = 0;
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst + wcslen(dst);
    while ((*d++ = *src++));
    return dst;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

int wctob(wint_t c)
{
    return (int)c;
}

wint_t btowc(int c)
{
    return (wint_t)c;
}

/* ---- epoll/eventfd stubs ---- */

int epoll_create(int size)
{
    (void)size;
    errno = ENOSYS;
    return -1;
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    (void)epfd;
    (void)op;
    (void)fd;
    (void)event;
    errno = ENOSYS;
    return -1;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    (void)epfd;
    (void)events;
    (void)maxevents;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

int epoll_create1(int flags)
{
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int eventfd(unsigned int initval, int flags)
{
    (void)initval;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int eventfd_read(int fd, eventfd_t *value)
{
    (void)fd;
    (void)value;
    errno = ENOSYS;
    return -1;
}

int eventfd_write(int fd, eventfd_t value)
{
    (void)fd;
    (void)value;
    errno = ENOSYS;
    return -1;
}

/* ---- regex stubs ---- */

int regcomp(regex_t *preg, const char *regex, int cflags)
{
    (void)preg;
    (void)regex;
    (void)cflags;
    return REG_ENOSYS;
}

int regexec(const regex_t *preg, const char *string, size_t nmatch,
            regmatch_t pmatch[], int eflags)
{
    (void)preg;
    (void)string;
    (void)nmatch;
    (void)pmatch;
    (void)eflags;
    return REG_ENOSYS;
}

size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size)
{
    static const char msg[] = "regex not implemented";
    size_t len = strlen(msg) + 1;
    if (errbuf && errbuf_size > 0) {
        strncpy(errbuf, msg, errbuf_size - 1);
        errbuf[errbuf_size - 1] = '\0';
    }
    return len;
}

void regfree(regex_t *preg)
{
    (void)preg;
}

/* ---- glob/fnmatch stubs ---- */

int glob(const char *pattern, int flags, int (*errfunc)(const char *epath, int eerrno), glob_t *pglob)
{
    (void)pattern;
    (void)flags;
    (void)errfunc;
    if (!pglob) return GLOB_NOSPACE;
    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
    return GLOB_NOMATCH;
}

void globfree(glob_t *pglob)
{
    if (pglob) {
        if (pglob->gl_pathv) {
            for (size_t i = 0; i < pglob->gl_pathc; i++) free(pglob->gl_pathv[i]);
            free(pglob->gl_pathv);
        }
        pglob->gl_pathc = 0;
        pglob->gl_pathv = NULL;
    }
}

int fnmatch(const char *pattern, const char *string, int flags)
{
    (void)pattern;
    (void)string;
    (void)flags;
    return FNM_NOMATCH;
}

/* ---- ifaddrs stubs ---- */

int getifaddrs(struct ifaddrs **ifap)
{
    if (!ifap) { errno = EINVAL; return -1; }
    errno = ENOSYS;
    return -1;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    (void)ifa;
}
