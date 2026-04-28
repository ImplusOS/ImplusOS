/*
 * NetSurf compatibility shim for ImplusOS
 * Provides missing libc functions needed by NetSurf and its dependencies
 */
#ifndef NETSURF_IMPLUSOS_COMPAT_H
#define NETSURF_IMPLUSOS_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/* --- ctype.h --- */
#include <ctype.h>
#ifndef isgraph
static inline int isgraph(int c) { return isprint(c) && c != ' '; }
#endif
#ifndef isascii
#define isascii(c) (((c) & ~0x7f) == 0)
#endif

/* --- Additional string functions --- */
size_t strnlen(const char *s, size_t maxlen);
char *strndup(const char *s, size_t n);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strpbrk(const char *s, const char *accept);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
int sscanf(const char *str, const char *format, ...);

/* --- setbuf / setvbuf --- */
#include <stdio.h>
static inline void setbuf(FILE *stream, char *buf) { (void)stream; (void)buf; }
static inline int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream; (void)buf; (void)mode; (void)size; return 0;
}

/* --- getopt --- */
extern char *optarg;
extern int optind, opterr, optopt;
struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};
#define no_argument       0
#define required_argument 1
#define optional_argument 2
int getopt(int argc, char *const argv[], const char *optstring);
int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex);

/* --- locale stub --- */
#define LC_ALL     0
#define LC_COLLATE 1
#define LC_CTYPE   2
#define LC_NUMERIC 4
#define LC_TIME    5
static inline char *setlocale(int category, const char *locale) {
    (void)category; (void)locale; return "C";
}

/* --- iconv stub (UTF-8 passthrough) --- */
typedef void *iconv_t;
iconv_t iconv_open(const char *tocode, const char *fromcode);
size_t iconv(iconv_t cd, char **inbuf, size_t *inbytesleft,
             char **outbuf, size_t *outbytesleft);
int iconv_close(iconv_t cd);

/* --- math --- */
#ifndef HUGE_VAL
#define HUGE_VAL __builtin_huge_val()
#endif
#ifndef INFINITY
#define INFINITY __builtin_inff()
#endif
#ifndef NAN
#define NAN __builtin_nanf("")
#endif
#ifndef HUGE_VALF
#define HUGE_VALF __builtin_huge_valf()
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
double log(double x);
double exp(double x);
double pow(double x, double y);
double sqrt(double x);
double fabs(double x);
double ceil(double x);
double floor(double x);
double fmod(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double round(double x);
double trunc(double x);
float roundf(float x);
float ceilf(float x);
float floorf(float x);
float fabsf(float x);
float sqrtf(float x);
int isnan(double x);
int isinf(double x);
int isfinite(double x);
long lround(double x);
long lroundf(float x);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double log10(double x);
double log2(double x);

/* --- misc --- */
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
long sysconf(int name);
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE 30

/* --- assert --- */
#ifndef assert
#include <assert.h>
#endif

/* --- va_list --- */
#include <stdarg.h>

/* sys/types.h already provided by ImplusOS libc */
#include <sys/types.h>

/* Types not in ImplusOS sys/types.h */
#ifndef _COMPAT_EXTRA_TYPES
#define _COMPAT_EXTRA_TYPES
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#endif

/* --- POSIX file constants --- */
#ifndef S_IRWXU
#define S_IRWXU  0700
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRWXG  0070
#define S_IRGRP  0040
#define S_IWGRP  0020
#define S_IXGRP  0010
#define S_IRWXO  0007
#define S_IROTH  0004
#define S_IWOTH  0002
#define S_IXOTH  0001
#endif

#ifndef MAP_SHARED
#define MAP_SHARED 0x01
#endif

#ifndef st_mtime
#define st_mtime st_size
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & 0170000) == 0100000)
#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISLNK(m) (0)
#define S_ISCHR(m) (0)
#define S_ISBLK(m) (0)
#define S_ISFIFO(m) (0)
#define S_ISSOCK(m) (0)
#endif

#ifndef S_IFREG
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#endif

/* --- AT_* flags --- */
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_FDCWD (-100)
#endif

/* --- PATH_MAX --- */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* --- FILENAME_MAX --- */
#ifndef FILENAME_MAX
#define FILENAME_MAX 256
#endif

/* --- dirent (provided by ImplusOS libc) --- */
#include <dirent.h>
#ifndef DT_LNK
#define DT_LNK 10
#endif

/* --- fstatat stub --- */
struct stat;
int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);

/* --- realpath stub --- */
char *realpath(const char *path, char *resolved_path);

/* --- access --- */
#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#endif
int access(const char *pathname, int mode);

/* --- tmpnam --- */
char *tmpnam(char *s);

/* --- rename --- */
int rename(const char *oldpath, const char *newpath);

/* --- BUFSIZ --- */
#ifndef BUFSIZ
#define BUFSIZ 8192
#endif

/* --- INT_MAX/MIN if not defined --- */
#ifndef RAND_MAX
#define RAND_MAX 2147483647
#endif
#ifndef INT_MAX
#define INT_MAX 2147483647
#define INT_MIN (-2147483647-1)
#endif
#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif
#ifndef LONG_MAX
#define LONG_MAX 9223372036854775807L
#define LONG_MIN (-LONG_MAX-1L)
#endif
#ifndef ULONG_MAX
#define ULONG_MAX 18446744073709551615UL
#endif

/* --- CHAR_BIT --- */
#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

/* --- PRIx64 etc --- */
#ifndef PRIx64
#define PRIx64 "lx"
#define PRId64 "ld"
#define PRIu64 "lu"
#define PRIx32 "x"
#define PRId32 "d"
#define PRIu32 "u"
#define PRIxPTR "lx"
#define PRIuPTR "lu"
#endif

/* --- signal --- */
#include <signal.h>
typedef void (*sighandler_t)(int);
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIG_IGN
#define SIG_IGN ((sighandler_t)1)
sighandler_t signal(int signum, sighandler_t handler);
#endif

/* --- setjmp --- */
#include <setjmp.h>
#ifndef _setjmp
#define _setjmp setjmp
#endif
#ifndef _longjmp
#define _longjmp longjmp
#endif

/* --- Missing string/time/dir functions --- */
struct tm;
struct dirent;

char *strcasestr(const char *haystack, const char *needle);
struct tm *localtime(const time_t *timer);
size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr);
int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **));

#endif /* NETSURF_IMPLUSOS_COMPAT_H */
