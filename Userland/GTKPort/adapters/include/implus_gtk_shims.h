#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

static inline char *getenv(const char *name) { (void)name; return (char*)0; }
static inline int   setenv(const char *n, const char *v, int o) { (void)n;(void)v;(void)o; return 0; }
static inline int   unsetenv(const char *n) { (void)n; return 0; }

#ifndef LC_ALL
#define LC_ALL    0
#define LC_CTYPE  1
#define LC_NUMERIC 2
#define LC_TIME   3
#define LC_COLLATE 4
#define LC_MONETARY 5
#define LC_MESSAGES 6
#endif
static inline char *setlocale(int cat, const char *loc) { (void)cat;(void)loc; return (char*)"C"; }

static inline unsigned getuid(void)  { return 0; }
static inline unsigned geteuid(void) { return 0; }
static inline unsigned getgid(void)  { return 0; }
static inline unsigned getegid(void) { return 0; }
static inline int      getpid(void)  { return 1; }

static inline int gethostname(char *buf, unsigned long len) {
    const char *h = "implus";
    unsigned long i;
    for (i = 0; i < len - 1 && h[i]; i++) buf[i] = h[i];
    buf[i] = 0;
    return 0;
}

static inline int pipe(int fd[2]) { (void)fd; return -1; }
static inline int pipe2(int fd[2], int flags) { (void)fd;(void)flags; return -1; }

static inline int access(const char *p, int m) { (void)p;(void)m; return -1; }
static inline int stat(const char *p, void *b)  { (void)p;(void)b; return -1; }
static inline int lstat(const char *p, void *b) { (void)p;(void)b; return -1; }
static inline int fstat(int fd, void *b) { (void)fd;(void)b; return -1; }
static inline int mkdir(const char *p, unsigned m) { (void)p;(void)m; return -1; }
static inline int rmdir(const char *p) { (void)p; return -1; }
static inline int unlink(const char *p) { (void)p; return -1; }
static inline int rename(const char *o, const char *n) { (void)o;(void)n; return -1; }
static inline int link(const char *o, const char *n) { (void)o;(void)n; return -1; }
static inline int symlink(const char *t, const char *l) { (void)t;(void)l; return -1; }
static inline long readlink(const char *p, char *b, unsigned long s) { (void)p;(void)b;(void)s; return -1; }
static inline int chmod(const char *p, unsigned m) { (void)p;(void)m; return -1; }
static inline int chown(const char *p, unsigned o, unsigned g) { (void)p;(void)o;(void)g; return -1; }
static inline long lseek(int fd, long off, int w) { (void)fd;(void)off;(void)w; return -1; }
static inline int ftruncate(int fd, long len) { (void)fd;(void)len; return -1; }
static inline int fsync(int fd) { (void)fd; return 0; }
static inline int fcntl(int fd, int cmd, ...) { (void)fd;(void)cmd; return -1; }
static inline int dup(int fd) { (void)fd; return -1; }
static inline int dup2(int o, int n) { (void)o;(void)n; return -1; }
static inline int isatty(int fd) { (void)fd; return 0; }
static inline int ioctl(int fd, unsigned long req, ...) { (void)fd;(void)req; return -1; }

static inline int fork(void) { return -1; }
static inline int execve(const char *p, char *const a[], char *const e[]) { (void)p;(void)a;(void)e; return -1; }
static inline int waitpid(int p, int *s, int o) { (void)p;(void)s;(void)o; return -1; }
static inline void _exit(int s) { for(;;); (void)s; }

#ifndef SIG_DFL
#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)
#define SIG_ERR ((void(*)(int))-1)
#define SIGPIPE 13
#define SIGHUP  1
#define SIGTERM 15
#define SIGINT  2
#endif
typedef void (*sighandler_t)(int);
static inline sighandler_t signal(int sig, sighandler_t h) { (void)sig;(void)h; return SIG_DFL; }

static inline int mkstemp(char *t) { (void)t; return -1; }
static inline char *mkdtemp(char *t) { (void)t; return (char*)0; }

#ifndef __IMPLUSOS_STRNDUP
#define __IMPLUSOS_STRNDUP
extern void *malloc(unsigned long);
static inline char *implus_strndup(const char *s, unsigned long n) {
    unsigned long i;
    for (i = 0; i < n && s[i]; i++) {}
    char *d = (char*)malloc(i + 1);
    if (d) { for (unsigned long j = 0; j < i; j++) d[j] = s[j]; d[i] = 0; }
    return d;
}
#define strndup implus_strndup
#endif

extern int snprintf(char *, unsigned long, const char *, ...);
extern int vsnprintf(char *, unsigned long, const char *, va_list);
