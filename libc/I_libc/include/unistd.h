#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

struct stat;

int32_t getpid(void);
int32_t getppid(void);
pid_t   fork(void);
void    _exit(int32_t status);
int     execve(const char *path, char *const argv[], char *const envp[]);
int     execv(const char *path, char *const argv[]);
int     execvp(const char *file, char *const argv[]);

void    sleep_ms(uint64_t milliseconds);

int open(const char* path, int flags, ...);
int creat(const char* path, int mode);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int isatty(int fd);

ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
unsigned int sleep(unsigned int seconds);
typedef unsigned int useconds_t;
int usleep(useconds_t usec);

char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int chroot(const char *path);

int access(const char *path, int mode);
int lstat(const char *path, struct stat *st);

int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsize);
int link(const char *oldpath, const char *newpath);

int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);

int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);

int openat(int dirfd, const char *path, int flags, ...);
int unlinkat(int dirfd, const char *path, int flags);
int mkdirat(int dirfd, const char *path, mode_t mode);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
