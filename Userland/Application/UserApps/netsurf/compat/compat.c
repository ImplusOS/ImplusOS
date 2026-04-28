/*
 * NetSurf compatibility shim implementation for ImplusOS
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <setjmp.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include "compat.h"

/* --- getopt implementation --- */
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char *const argv[], const char *optstring) {
    static int optpos = 0;
    if (optind >= argc || argv[optind] == NULL) return -1;
    const char *arg = argv[optind];
    if (arg[0] != '-' || arg[1] == '\0') return -1;
    if (arg[1] == '-' && arg[2] == '\0') { optind++; return -1; }
    char c;
    if (optpos == 0) optpos = 1;
    c = arg[optpos];
    const char *p = strchr(optstring, c);
    if (!p) { optopt = c; optpos = 0; optind++; return '?'; }
    if (p[1] == ':') {
        if (arg[optpos + 1] != '\0') {
            optarg = (char *)&arg[optpos + 1];
        } else if (optind + 1 < argc) {
            optind++;
            optarg = argv[optind];
        } else {
            optopt = c; optpos = 0; optind++; return '?';
        }
        optpos = 0; optind++;
    } else {
        optpos++;
        if (arg[optpos] == '\0') { optpos = 0; optind++; }
        optarg = NULL;
    }
    return c;
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex) {
    if (optind >= argc) return -1;
    const char *arg = argv[optind];
    if (arg[0] != '-') return -1;
    if (arg[1] == '-' && arg[2] != '\0') {
        const char *name = arg + 2;
        for (int i = 0; longopts[i].name; i++) {
            size_t nlen = strlen(longopts[i].name);
            if (strncmp(name, longopts[i].name, nlen) == 0 &&
                (name[nlen] == '\0' || name[nlen] == '=')) {
                if (longindex) *longindex = i;
                if (longopts[i].has_arg && name[nlen] == '=') {
                    optarg = (char *)name + nlen + 1;
                } else if (longopts[i].has_arg == 1) {
                    optind++;
                    if (optind < argc) optarg = argv[optind];
                    else return '?';
                }
                optind++;
                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                return longopts[i].val;
            }
        }
        optind++;
        return '?';
    }
    return getopt(argc, argv, optstring);
}

/* --- string / memory functions --- */
size_t strnlen(const char *s, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n && *s1 && *s2; i++, s1++, s2++) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
    }
    return 0;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        if (strchr(accept, *s)) return (char *)s;
        s++;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept) {
    size_t n = 0;
    while (*s && strchr(accept, *s++)) n++;
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    size_t n = 0;
    while (*s && !strchr(reject, *s++)) n++;
    return n;
}

/* --- math --- */
float ceilf(float x) {
    int i = (int)x;
    if (x > (float)i) return (float)(i + 1);
    return (float)i;
}

/* --- stdio / conversion --- */
long long strtoll(const char *nptr, char **endptr, int base) { return (long long)strtol(nptr, endptr, base); }
unsigned long long strtoull(const char *nptr, char **endptr, int base) { return (unsigned long long)strtoul(nptr, endptr, base); }
double strtod(const char *nptr, char **endptr) { return 0.0; }
float strtof(const char *nptr, char **endptr) { return (float)strtod(nptr, endptr); }

int sscanf(const char *str, const char *format, ...) { return 0; }

char *tmpnam(char *s) {
    static char buf[64];
    static int counter = 0;
    if (!s) s = buf;
    snprintf(s, 64, "/tmp/ns_%d", counter++);
    return s;
}

/* --- time --- */
struct tm *localtime(const time_t *timer) { static struct tm t; memset(&t, 0, sizeof(t)); return &t; }
struct tm *gmtime(const time_t *timer) { return localtime(timer); }
size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr) { if (maxsize > 0) *s = '\0'; return 0; }

/* --- dirent --- */
int dirfd(DIR *dirp) { return -1; }

/* --- file / posix --- */
int rename(const char *oldpath, const char *newpath) { return -1; }
int unlinkat(int dirfd, const char *pathname, int flags) { return -1; }
int rmdir(const char *pathname) { return -1; }
ssize_t pread(int fd, void *buf, size_t count, off_t offset) { return -1; }
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) { return -1; }
int access(const char *pathname, int mode) { return 0; }
int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) { return -1; }

/* --- sysconf --- */
long sysconf(int name) { if (name == 30) return 4096; return -1; }

/* --- setjmp / longjmp stubs --- */
#undef setjmp
#undef longjmp
int setjmp(jmp_buf env) { (void)env; return 0; }
void longjmp(jmp_buf env, int val) { (void)env; (void)val; while(1); }
#undef __longjmp_chk
void __longjmp_chk(jmp_buf env, int val) { longjmp(env, val); }

/* Aliases for _setjmp and _longjmp used by some libraries like FreeType */
#undef _setjmp
#undef _longjmp
int _setjmp(jmp_buf env) { return setjmp(env); }
void _longjmp(jmp_buf env, int val) { longjmp(env, val); }

/* --- NetSurf / Libs specific --- */
int hubbub_error_from_parserutils_error(int error) {
    if (error == 0) return 0;
    if (error == 1) return 1;
    if (error == 2) return 2;
    if (error == 3) return 3;
    if (error == 4) return 4;
    if (error == 5) return 5;
    if (error == 6) return 6;
    if (error == 7) return 0;
    return 10;
}

/* --- iconv stub --- */
typedef void *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode) { return (iconv_t)-1; }
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft) {
    if (!inbuf || !*inbuf || !outbuf || !*outbuf) return 0;
    size_t n = (*inbytesleft < *outbytesleft) ? *inbytesleft : *outbytesleft;
    memcpy(*outbuf, *inbuf, n);
    *inbuf += n; *outbuf += n;
    *inbytesleft -= n; *outbytesleft -= n;
    return 0;
}
int iconv_close(iconv_t cd) { return 0; }

/* --- other stubs --- */
int atexit(void (*func)(void)) { return 0; }
int uname(void *buf) { return -1; }
bool save_pdf(const char *path) { return false; }
int fetch_javascript_register(void) { return 0; }

/* --- regex stubs --- */
typedef struct { int dummy; } regex_t;
typedef struct { int dummy; } regmatch_t;
int regcomp(regex_t *preg, const char *regex, int cflags) { return -1; }
int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags) { return -1; }
size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size) { if(errbuf&&errbuf_size) errbuf[0]=0; return 0; }
void regfree(regex_t *preg) { }
