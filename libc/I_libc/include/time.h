#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int64_t time_t;
typedef int64_t clock_t;

#ifndef __clockid_t_defined
typedef int clockid_t;
#define __clockid_t_defined 1
#endif

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCKS_PER_SEC           1000

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

typedef struct timespec timespec_t;

uint64_t    clock_ms(void);
clock_t     clock(void);
time_t      time(time_t* out);
int         nanosleep(const struct timespec* req, struct timespec* rem);
int         clock_gettime(clockid_t clk_id, struct timespec* tp);
int         clock_settime(clockid_t clk_id, const struct timespec* tp);
int         clock_getres (clockid_t clk_id, struct timespec* res);
struct tm  *gmtime_r    (const time_t* timep, struct tm* result);
struct tm  *localtime_r (const time_t* timep, struct tm* result);
struct tm  *gmtime      (const time_t* timep);
struct tm  *localtime   (const time_t* timep);
time_t      mktime      (struct tm* tm);
double      difftime    (time_t t1, time_t t0);
size_t      strftime    (char* s, size_t max, const char* format, const struct tm* tm);
