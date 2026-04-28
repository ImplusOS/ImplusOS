#pragma once
#include <stdint.h>

struct input_event {
    uint64_t time_sec;
    uint64_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};

static inline int implus_evdev_open(const char *path) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(210ULL), "D"((long)path) : "rcx","r11","memory");
    return (int)r;
}

static inline long implus_evdev_read(int fd, void *buf, unsigned long len) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(211ULL), "D"((long)fd), "S"((long)buf), "d"((long)len) : "rcx","r11","memory");
    return r;
}

static inline int implus_evdev_close(int fd) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(213ULL), "D"((long)fd) : "rcx","r11","memory");
    return (int)r;
}
