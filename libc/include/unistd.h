#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

int32_t getpid(void);
int32_t getppid(void);
void    _exit(int32_t status);

void    sleep_ms(uint64_t milliseconds);

int open(const char* path, int flags, ...);
int creat(const char* path, int mode);
ssize_t read(int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int pipe(int pipefd[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
unsigned int sleep(unsigned int seconds);
typedef unsigned int useconds_t;
int usleep(useconds_t usec);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
