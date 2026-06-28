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

int bcmp(const void* s1, const void* s2, size_t n)
{
    return memcmp(s1, s2, n);
}

void bcopy(const void* src, void* dst, size_t n)
{
    (void)memmove(dst, src, n);
}

void bzero(void* s, size_t n)
{
    (void)memset(s, 0, n);
}

int ffs(int i)
{
    unsigned int value = (unsigned int)i;

    if (value == 0)
        return 0;

    int bit = 1;
    while ((value & 1U) == 0U) {
        value >>= 1;
        bit++;
    }

    return bit;
}

size_t strlen(const char* s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

size_t strnlen(const char* s, size_t max_len)
{
    size_t n = 0;
    if (s == 0) return 0;
    while (n < max_len && s[n]) n++;
    return n;
}

size_t strlcpy(char* dst, const char* src, size_t dst_size)
{
    size_t src_len = strlen(src);
    if (dst_size != 0) {
        size_t copy_len = src_len < dst_size - 1 ? src_len : dst_size - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

size_t strlcat(char* dst, const char* src, size_t dst_size)
{
    size_t dst_len = strnlen(dst, dst_size);
    size_t src_len = strlen(src);
    if (dst_len == dst_size) return dst_size + src_len;
    if (dst_size > dst_len + 1) {
        size_t room = dst_size - dst_len - 1;
        size_t copy_len = src_len < room ? src_len : room;
        memcpy(dst + dst_len, src, copy_len);
        dst[dst_len + copy_len] = '\0';
    }
    return dst_len + src_len;
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

int strcasecmp(const char* a, const char* b)
{
    if (a == 0 || b == 0) return 0;
    while (*a && *b) {
        unsigned char c1 = (unsigned char)*a;
        unsigned char c2 = (unsigned char)*b;
        if (c1 >= 'A' && c1 <= 'Z') c1 = (unsigned char)(c1 + ('a' - 'A'));
        if (c2 >= 'A' && c2 <= 'Z') c2 = (unsigned char)(c2 + ('a' - 'A'));
        if (c1 != c2)
            return (int)c1 - (int)c2;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncasecmp(const char* a, const char* b, size_t n)
{
    if (a == 0 || b == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)a[i];
        unsigned char c2 = (unsigned char)b[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 = (unsigned char)(c1 + ('a' - 'A'));
        if (c2 >= 'A' && c2 <= 'Z') c2 = (unsigned char)(c2 + ('a' - 'A'));
        if (c1 != c2)
            return (int)c1 - (int)c2;
        if (c1 == 0)
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
    return strtok_r(str, delim, &g_strtok_save);
}

char* strtok_r(char* str, const char* delim, char** saveptr)
{
    if (!saveptr) return NULL;
    if (str != NULL) *saveptr = str;
    if (*saveptr == NULL || **saveptr == '\0') return NULL;

    char* p = *saveptr;
    while (*p) {
        int is_delim = 0;
        for (const char* d = delim; *d; d++) {
            if (*p == *d) { is_delim = 1; break; }
        }
        if (!is_delim) break;
        p++;
    }
    if (*p == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char* token = p;
    while (*p) {
        int is_delim = 0;
        for (const char* d = delim; *d; d++) {
            if (*p == *d) { is_delim = 1; break; }
        }
        if (is_delim) {
            *p = '\0';
            *saveptr = p + 1;
            return token;
        }
        p++;
    }
    *saveptr = NULL;
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

char* strndup(const char* s, size_t n)
{
    if (s == 0) return 0;
    size_t len = strnlen(s, n);
    char* d = (char*)malloc(len + 1);
    if (d == 0) return 0;
    memcpy(d, s, len);
    d[len] = '\0';
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
        case 3:  return "No such process";
        case 4:  return "Interrupted system call";
        case 5:  return "I/O error";
        case 6:  return "No such device or address";
        case 7:  return "Argument list too long";
        case 8:  return "Exec format error";
        case 9:  return "Bad file number";
        case 10: return "No child processes";
        case 11: return "Try again";
        case 12: return "Out of memory";
        case 13: return "Permission denied";
        case 14: return "Bad address";
        case 16: return "Device or resource busy";
        case 17: return "File exists";
        case 22: return "Invalid argument";
        case 23: return "File table overflow";
        case 24: return "Too many open files";
        case 34: return "Result too large";
        case 36: return "File name too long";
        case 38: return "Function not implemented";
        case 39: return "Directory not empty";
        case 40: return "Too many levels of symbolic links";
        case 75: return "Value too large for defined data type";
        case 84: return "Invalid or incomplete multibyte or wide character";
        case 89: return "Destination address required";
        case 90: return "Message too long";
        case 93: return "Protocol not supported";
        case 95: return "Operation not supported";
        case 97: return "Address family not supported";
        case 98: return "Address already in use";
        case 99: return "Cannot assign requested address";
        case 100: return "Network is down";
        case 101: return "Network is unreachable";
        case 107: return "Transport endpoint is not connected";
        case 110: return "Connection timed out";
        case 111: return "Connection refused";
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

void* memrchr(const void* s, int c, size_t n)
{
    const unsigned char* p = (const unsigned char*)s;
    if (n == 0) return NULL;
    for (size_t i = n; i != 0; i--) {
        if (p[i - 1] == (unsigned char)c) return (void*)(p + i - 1);
    }
    return NULL;
}

size_t strspn(const char* s, const char* accept)
{
    const char* p = s;
    while (*p) {
        int found = 0;
        for (const char* a = accept; *a; a++) {
            if (*p == *a) { found = 1; break; }
        }
        if (!found) break;
        p++;
    }
    return (size_t)(p - s);
}

size_t strcspn(const char* s, const char* reject)
{
    const char* p = s;
    while (*p) {
        for (const char* r = reject; *r; r++) {
            if (*p == *r) return (size_t)(p - s);
        }
        p++;
    }
    return (size_t)(p - s);
}

char* strpbrk(const char* s, const char* accept)
{
    while (*s) {
        for (const char* a = accept; *a; a++) {
            if (*s == *a) return (char*)s;
        }
        s++;
    }
    return NULL;
}

const char* strsignal(int signum)
{
    switch (signum) {
        case 1:  return "Hangup";
        case 2:  return "Interrupt";
        case 3:  return "Quit";
        case 4:  return "Illegal instruction";
        case 5:  return "Trace/breakpoint trap";
        case 6:  return "Aborted";
        case 7:  return "Bus error";
        case 8:  return "Floating point exception";
        case 9:  return "Killed";
        case 10: return "User defined signal 1";
        case 11: return "Segmentation fault";
        case 12: return "User defined signal 2";
        case 13: return "Broken pipe";
        case 14: return "Alarm clock";
        case 15: return "Terminated";
        case 16: return "Stack fault";
        case 17: return "Child exited";
        case 18: return "Continued";
        case 19: return "Stopped (signal)";
        case 20: return "Stopped";
        case 21: return "Stopped (tty input)";
        case 22: return "Stopped (tty output)";
        case 23: return "Urgent I/O condition";
        case 24: return "CPU time limit exceeded";
        case 25: return "File size limit exceeded";
        case 26: return "Virtual timer expired";
        case 27: return "Profiling timer expired";
        case 28: return "Window changed";
        case 29: return "I/O possible";
        case 30: return "Power failure";
        case 31: return "Bad system call";
        default: return "Unknown signal";
    }
}
