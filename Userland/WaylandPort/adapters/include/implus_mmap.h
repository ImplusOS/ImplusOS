#pragma once
#include <stdint.h>
#include <stddef.h>

#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_SHARED    0x01
#define MAP_FAILED ((void*)-1)

static inline void *implus_mmap(void *addr, size_t len, int prot, int flags, int fd, long offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    extern long __implus_musl_syscall(long,long,long,long,long,long,long);
    long r = __implus_musl_syscall(9, (long)addr, (long)len, (long)prot, (long)flags, (long)fd, (long)offset);
    return r < 0 ? MAP_FAILED : (void*)r;
}

static inline int implus_munmap(void *addr, size_t len) {
    extern long __implus_musl_syscall(long,long,long,long,long,long,long);
    return (int)__implus_musl_syscall(11, (long)addr, (long)len, 0, 0, 0, 0);
}

#define mmap implus_mmap
#define munmap implus_munmap
