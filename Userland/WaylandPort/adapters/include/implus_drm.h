#pragma once
#include <stdint.h>

#define DRM_IOCTL_BASE 0xC0

static inline int implus_drm_open(void) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(200ULL) : "rcx","r11","memory");
    return (int)r;
}

static inline long implus_drm_ioctl(int fd, unsigned long req, void *arg) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(201ULL), "D"((long)fd), "S"((long)req), "d"((long)arg) : "rcx","r11","memory");
    return r;
}

static inline void *implus_drm_mmap(int fd, uint64_t offset, uint64_t size) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(203ULL), "D"((long)fd), "S"((long)offset), "d"((long)size) : "rcx","r11","memory");
    return (void*)r;
}

static inline int implus_drm_close(int fd) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(202ULL), "D"((long)fd) : "rcx","r11","memory");
    return (int)r;
}
