#include <string.h>
#include <stdint.h>

void* memset(void* ptr, int v, size_t n)
{
    unsigned char* p = (unsigned char*)ptr;
    unsigned char c = (unsigned char)v;

    
    if (n < 16) {
        for (size_t i = 0; i < n; i++)
            p[i] = c;
        return ptr;
    }

    
    size_t head = (8 - ((uintptr_t)p & 7)) & 7;
    for (size_t i = 0; i < head; i++)
        p[i] = c;

    
    uint64_t fill = (uint64_t)c * 0x0101010101010101ULL;
    uint64_t *wp = (uint64_t *)(p + head);
    size_t words = (n - head) / 8;
    size_t tail  = (n - head) % 8;

    for (size_t i = 0; i < words; i++)
        wp[i] = fill;

    
    unsigned char *tp = (unsigned char *)(wp + words);
    for (size_t i = 0; i < tail; i++)
        tp[i] = c;

    return ptr;
}

void* memmove(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        
        
        size_t head = 0;
        while (head < n && ((uintptr_t)(d + head) & 7) != 0) {
            d[head] = s[head];
            head++;
        }
        
        size_t remaining = n - head;
        size_t words = remaining / 8;
        size_t tail  = remaining % 8;
        uint64_t *dw = (uint64_t *)(d + head);
        const uint64_t *sw = (const uint64_t *)(s + head);
        for (size_t i = 0; i < words; i++)
            dw[i] = sw[i];
        
        unsigned char *dt = (unsigned char *)(dw + words);
        const unsigned char *st = (const unsigned char *)(sw + words);
        for (size_t i = 0; i < tail; i++)
            dt[i] = st[i];
    } else {
        
        size_t tail_bytes = 0;
        while (tail_bytes < n && ((uintptr_t)(d + n - tail_bytes) & 7) != 0) {
            tail_bytes++;
            d[n - tail_bytes] = s[n - tail_bytes];
        }
        size_t remaining = n - tail_bytes;
        size_t words = remaining / 8;
        size_t head  = remaining % 8;
        uint64_t *dw = (uint64_t *)(d + head);
        const uint64_t *sw = (const uint64_t *)(s + head);
        for (size_t i = words; i != 0; i--)
            dw[i - 1] = sw[i - 1];
        for (size_t i = head; i != 0; i--)
            d[i - 1] = s[i - 1];
    }

    return dst;
}

void* memcpy(void* dst, const void* src, size_t n)
{
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0)
        return dst;

    
    size_t head = 0;
    while (head < n && ((uintptr_t)(d + head) & 7) != 0) {
        d[head] = s[head];
        head++;
    }
    size_t remaining = n - head;
    size_t words = remaining / 8;
    size_t tail  = remaining % 8;
    uint64_t *dw = (uint64_t *)(d + head);
    const uint64_t *sw = (const uint64_t *)(s + head);
    for (size_t i = 0; i < words; i++)
        dw[i] = sw[i];
    unsigned char *dt = (unsigned char *)(dw + words);
    const unsigned char *st = (const unsigned char *)(sw + words);
    for (size_t i = 0; i < tail; i++)
        dt[i] = st[i];

    return dst;
}

int memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* p1 = (const unsigned char*)a;
    const unsigned char* p2 = (const unsigned char*)b;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i])
            return (int)p1[i] - (int)p2[i];
    }

    return 0;
}

size_t strlen(const char* s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

char* strcpy(char* d, const char* s)
{
    if (d == 0 || s == 0) return d;
    char* r = d;
    while ((*d++ = *s++));
    return r;
}

char* strncpy(char* d, const char* s, size_t n)
{
    if (d == 0 || s == 0) return d;
    size_t i = 0;

    for (; i < n && s[i]; i++)
        d[i] = s[i];

    for (; i < n; i++)
        d[i] = 0;

    return d;
}

int strcmp(const char* a, const char* b)
{
    if (a == 0 || b == 0) return 0;
    while (*a && (*a == *b)) {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n)
{
    if (a == 0 || b == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0)
            return 0;
    }
    return 0;
}

char* strchr(const char* s, int c)
{
    if (s == 0) return 0;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : 0;
}

char* strrchr(const char* s, int c)
{
    if (s == 0) return 0;
    const char* last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle)
{
    if (haystack == 0 || needle == 0) return 0;
    if (*needle == 0) return (char*)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, nlen) == 0)
            return (char*)haystack;
        haystack++;
    }
    return 0;
}

static char* g_strtok_save = 0;

char* strtok(char* str, const char* delim)
{
    if (str != 0) g_strtok_save = str;
    if (g_strtok_save == 0 || *g_strtok_save == 0) return 0;
    
    while (*g_strtok_save) {
        int is_delim = 0;
        for (const char* d = delim; *d; d++) {
            if (*g_strtok_save == *d) { is_delim = 1; break; }
        }
        if (!is_delim) break;
        g_strtok_save++;
    }
    if (*g_strtok_save == 0) return 0;

    char* token = g_strtok_save;
    while (*g_strtok_save) {
        for (const char* d = delim; *d; d++) {
            if (*g_strtok_save == *d) {
                *g_strtok_save = 0;
                g_strtok_save++;
                return token;
            }
        }
        g_strtok_save++;
    }
    return token;
}

extern void* malloc(size_t);

char* strdup(const char* s)
{
    if (s == 0) return 0;
    size_t len = strlen(s) + 1;
    char* d = (char*)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

char* strcat(char* dst, const char* src)
{
    if (dst == 0 || src == 0) return dst;
    char* d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char* strncat(char* dst, const char* src, size_t n)
{
    if (dst == 0 || src == 0) return dst;
    char* d = dst + strlen(dst);
    size_t i = 0;
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    *d = 0;
    return dst;
}

const char* strerror(int errnum)
{
    switch (errnum) {
        case 0:  return "Success";
        case 1:  return "Operation not permitted";
        case 2:  return "No such file or directory";
        case 5:  return "I/O error";
        case 12: return "Out of memory";
        case 13: return "Permission denied";
        case 14: return "Bad address";
        case 22: return "Invalid argument";
        case 24: return "Too many open files";
        case 95: return "Operation not supported";
        default: return "Unknown error";
    }
}

void* memchr(const void* s, int c, size_t n)
{
    const unsigned char* p = (const unsigned char*)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void*)(p + i);
    }
    return 0;
}

