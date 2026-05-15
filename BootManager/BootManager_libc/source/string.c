#include "../include/string.h"

#define BM_LIBC_WEAK __attribute__((weak))

BM_LIBC_WEAK void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dest;
}

BM_LIBC_WEAK void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dest;
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dest;
}

BM_LIBC_WEAK void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return s;
}

BM_LIBC_WEAK int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

BM_LIBC_WEAK size_t strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

BM_LIBC_WEAK char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != 0) {}
    return dest;
}

BM_LIBC_WEAK int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        ++s1;
        ++s2;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}
