#pragma once
#include <stdint.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#define TIMER_ABSTIME 1

static inline int clock_gettime(int clk, struct timespec *tp) {
    long r;
    __asm__ volatile("syscall":"=a"(r):"a"(164ULL),"D"((long)clk),"S"((long)tp):"rcx","r11","memory");
    return (int)r;
}

static inline uint64_t implus_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
