#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifdef KERNEL

void exit(int status) {
    (void)status;
    for(;;);
}

void abort(void) {
    exit(1);
}

#else
extern void* syscall1(uint64_t num, uint64_t arg1);
extern void* syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall0(uint64_t num);
#define SYSCALL_USER_MALLOC  27ULL
#define SYSCALL_USER_FREE    28ULL
#define SYSCALL_PROCESS_YIELD 7ULL
#define SYSCALL_PROCESS_EXIT  8ULL
#define SYSCALL_USER_MMAP    43ULL

typedef struct malloc_block {
    size_t size;
    int free;
    struct malloc_block *next;
} malloc_block_t;

static malloc_block_t *free_list = NULL;
static volatile int malloc_lock_state;

static void malloc_lock(void)
{
    while (__sync_lock_test_and_set(&malloc_lock_state, 1)) {
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

static void malloc_unlock(void)
{
    __sync_lock_release(&malloc_lock_state);
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 15u) & ~((size_t)15u);
    malloc_lock();
    malloc_block_t *curr = free_list;
    while (curr) {
        if (curr->free && curr->size >= size) {
            curr->free = 0;
            malloc_unlock();
            return (void*)(curr + 1);
        }
        curr = curr->next;
    }
    size_t alloc_size = size + sizeof(malloc_block_t);
    alloc_size = (alloc_size + 4095u) & ~((size_t)4095u);
    malloc_block_t *block = (malloc_block_t*)syscall2(SYSCALL_USER_MMAP, alloc_size, 0);
    if (!block) {
        malloc_unlock();
        return NULL;
    }
    block->size = alloc_size - sizeof(malloc_block_t);
    block->free = 0;
    block->next = free_list;
    free_list = block;
    malloc_unlock();
    return (void*)(block + 1);
}

void* calloc(size_t nmemb, size_t size)
{
    size_t total;
    void* ptr;

    if (nmemb == 0 || size == 0) {
        return malloc(1);
    }
    total = nmemb * size;
    if (size != 0 && total / size != nmemb) {
        return NULL;
    }
    ptr = malloc(total);
    if (ptr) {
        unsigned char* p = (unsigned char*)ptr;
        for (size_t i = 0; i < total; ++i) {
            p[i] = 0;
        }
    }
    return ptr;
}

void* realloc(void* ptr, size_t size)
{
    malloc_block_t* block;
    void* new_ptr;
    size_t old_size;

    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block = (malloc_block_t*)ptr - 1;
    old_size = block->size;
    if (old_size >= size) {
        return ptr;
    }

    new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    {
        unsigned char* dst = (unsigned char*)new_ptr;
        unsigned char* src = (unsigned char*)ptr;
        for (size_t i = 0; i < old_size; ++i) {
            dst[i] = src[i];
        }
    }

    free(ptr);
    return new_ptr;
}

void free(void* p) {
    if (!p) return;
    malloc_lock();
    malloc_block_t *block = (malloc_block_t*)p - 1;
    block->free = 1;
    malloc_unlock();
}

void exit(int status) {
    (void)syscall1(SYSCALL_PROCESS_EXIT, (uint64_t)status);
    for(;;);
}

void abort(void) {
    exit(1);
}
#endif

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long acc;
    int c;
    unsigned long cutoff;
    int neg = 0, any, cutlim;

    do {
        c = *s++;
    } while (c == ' ' || (c >= '\t' && c <= '\r'));

    if (c == '-') {
        neg = 1;
        c = *s++;
    } else if (c == '+') {
        c = *s++;
    }

    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }

    if (base == 0) {
        base = c == '0' ? 8 : 10;
    }

    cutoff = neg ? -(unsigned long)0x80000000 : 0x7FFFFFFF;
    cutlim = (int)(cutoff % (unsigned long)base);
    cutoff /= (unsigned long)base;

    for (acc = 0, any = 0;; c = *s++) {
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'A' && c <= 'Z')
            c -= 'A' - 10;
        else if (c >= 'a' && c <= 'z')
            c -= 'a' - 10;
        else
            break;
        if (c >= base)
            break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
            any = -1;
        else {
            any = 1;
            acc *= (unsigned long)base;
            acc += (unsigned long)c;
        }
    }
    if (any < 0) {
        acc = neg ? 0x80000000 : 0x7FFFFFFF;
    } else if (neg) {
        acc = (unsigned long)-(long)acc;
    }
    if (endptr != 0) {
        *endptr = (char*)(any ? s - 1 : nptr);
    }
    return (long)acc;
}

unsigned long strtoul(const char* nptr, char** endptr, int base)
{
    const char* s = nptr;
    unsigned long acc = 0;
    int c;
    int any = 0;

    do {
        c = *s++;
    } while (c == ' ' || (c >= '\t' && c <= '\r'));

    if (c == '+') {
        c = *s++;
    }

    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }

    if (base == 0) {
        base = c == '0' ? 8 : 10;
    }

    for (;; c = *s++) {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'A' && c <= 'Z') {
            c -= 'A' - 10;
        } else if (c >= 'a' && c <= 'z') {
            c -= 'a' - 10;
        } else {
            break;
        }
        if (c >= base) {
            break;
        }
        any = 1;
        acc = acc * (unsigned long)base + (unsigned long)c;
    }

    if (endptr != 0) {
        *endptr = (char*)(any ? s - 1 : nptr);
    }
    return acc;
}

long long strtoll(const char* nptr, char** endptr, int base)
{
    const char* s = nptr;
    unsigned long long acc;
    int c;
    unsigned long long cutoff;
    int neg = 0, any, cutlim;

    do {
        c = *s++;
    } while (c == ' ' || (c >= '\t' && c <= '\r'));

    if (c == '-') {
        neg = 1;
        c = *s++;
    } else if (c == '+') {
        c = *s++;
    }

    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }

    if (base == 0) {
        base = c == '0' ? 8 : 10;
    }

    cutoff = neg ? -(unsigned long long)0x8000000000000000LL : 0x7FFFFFFFFFFFFFFFLL;
    cutlim = (int)(cutoff % (unsigned long long)base);
    cutoff /= (unsigned long long)base;

    for (acc = 0, any = 0;; c = *s++) {
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'A' && c <= 'Z')
            c -= 'A' - 10;
        else if (c >= 'a' && c <= 'z')
            c -= 'a' - 10;
        else
            break;
        if (c >= base)
            break;
        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
            any = -1;
        else {
            any = 1;
            acc *= (unsigned long long)base;
            acc += (unsigned long long)c;
        }
    }
    if (any < 0) {
        acc = neg ? 0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFULL;
    } else if (neg) {
        acc = (unsigned long long)-(long long)acc;
    }
    if (endptr != 0) {
        *endptr = (char*)(any ? s - 1 : nptr);
    }
    return (long long)acc;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base)
{
    const char* s = nptr;
    unsigned long long acc = 0;
    int c;
    int any = 0;

    do {
        c = *s++;
    } while (c == ' ' || (c >= '\t' && c <= '\r'));

    if (c == '+') {
        c = *s++;
    }

    if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }

    if (base == 0) {
        base = c == '0' ? 8 : 10;
    }

    for (;; c = *s++) {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'A' && c <= 'Z') {
            c -= 'A' - 10;
        } else if (c >= 'a' && c <= 'z') {
            c -= 'a' - 10;
        } else {
            break;
        }
        if (c >= base) {
            break;
        }
        any = 1;
        acc = acc * (unsigned long long)base + (unsigned long long)c;
    }

    if (endptr != 0) {
        *endptr = (char*)(any ? s - 1 : nptr);
    }
    return acc;
}

double strtod(const char* nptr, char** endptr)
{
    const char* s = nptr;
    double val = 0.0;
    int neg = 0;
    int any = 0;
    int c;

    do {
        c = *s++;
    } while (c == ' ' || (c >= '\t' && c <= '\r'));

    if (c == '-') { neg = 1; c = *s++; }
    else if (c == '+') { c = *s++; }

    while (c >= '0' && c <= '9') {
        val = val * 10.0 + (double)(c - '0');
        any = 1;
        c = *s++;
    }

    if (c == '.') {
        double frac = 0.0;
        double div = 10.0;
        c = *s++;
        while (c >= '0' && c <= '9') {
            frac += (double)(c - '0') / div;
            div *= 10.0;
            any = 1;
            c = *s++;
        }
        val += frac;
    }

    if ((c == 'e' || c == 'E') && any) {
        int exp_neg = 0;
        int exp_val = 0;
        c = *s++;
        if (c == '-') { exp_neg = 1; c = *s++; }
        else if (c == '+') { c = *s++; }
        while (c >= '0' && c <= '9') {
            exp_val = exp_val * 10 + (c - '0');
            c = *s++;
        }
        double multiplier = 1.0;
        int e = exp_val;
        if (exp_neg) {
            while (e-- > 0) multiplier /= 10.0;
        } else {
            while (e-- > 0) multiplier *= 10.0;
        }
        val *= multiplier;
    }

    s--;
    if (endptr) *endptr = (char*)(any ? s : nptr);
    return neg ? -val : val;
}

float strtof(const char* nptr, char** endptr)
{
    return (float)strtod(nptr, endptr);
}

long double strtold(const char* nptr, char** endptr)
{
    return (long double)strtod(nptr, endptr);
}

double atof(const char* nptr)
{
    return strtod(nptr, NULL);
}

int atoi(const char* nptr) {
    return (int)strtol(nptr, (char**)0, 10);
}

long atol(const char* nptr) {
    return strtol(nptr, (char**)0, 10);
}

int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

long long llabs(long long n) {
    return n < 0 ? -n : n;
}

div_t div(int num, int denom)
{
    div_t r;
    r.quot = num / denom;
    r.rem = num % denom;
    return r;
}

ldiv_t ldiv(long num, long denom)
{
    ldiv_t r;
    r.quot = num / denom;
    r.rem = num % denom;
    return r;
}

lldiv_t lldiv(long long num, long long denom)
{
    lldiv_t r;
    r.quot = num / denom;
    r.rem = num % denom;
    return r;
}

static void qsort_swap(unsigned char* a, unsigned char* b, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        unsigned char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

static void qsort_impl(unsigned char* base, size_t nmemb, size_t size,
                       int (*compar)(const void*, const void*))
{
    while (nmemb > 1) {
        size_t left = 0;
        size_t right = nmemb - 1;
        size_t pivot_idx = nmemb / 2;

        qsort_swap(base + pivot_idx * size, base + right * size, size);

        size_t store = left;
        for (size_t i = left; i < right; ++i) {
            if (compar(base + i * size, base + right * size) < 0) {
                qsort_swap(base + i * size, base + store * size, size);
                store++;
            }
        }
        qsort_swap(base + store * size, base + right * size, size);

        if (store > nmemb / 2) {
            qsort_impl(base, store, size, compar);
            base += (store + 1) * size;
            nmemb -= store + 1;
        } else {
            qsort_impl(base + (store + 1) * size, nmemb - store - 1, size, compar);
            nmemb = store;
        }
    }
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    if (!base || !compar || size == 0 || nmemb < 2) {
        return;
    }
    qsort_impl((unsigned char*)base, nmemb, size, compar);
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*))
{
    const unsigned char* bytes = (const unsigned char*)base;
    size_t left = 0;
    size_t right = nmemb;

    if (!key || !base || !compar || size == 0) {
        return NULL;
    }

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const void* elem = bytes + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) {
            return (void*)elem;
        }
        if (cmp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }

    return NULL;
}

static char g_env_buf[4096];
static size_t g_env_len = 0;

static int env_find(const char *name, size_t *out_val_start)
{
    size_t name_len = strlen(name);
    size_t pos = 0;
    while (pos < g_env_len) {
        const char *eq = strchr(&g_env_buf[pos], '=');
        if (!eq) break;
        size_t var_len = (size_t)(eq - &g_env_buf[pos]);
        if (var_len == name_len && strncmp(&g_env_buf[pos], name, name_len) == 0) {
            if (out_val_start) *out_val_start = pos;
            return 1;
        }
        pos += var_len + 1 + strlen(eq + 1) + 1;
    }
    return 0;
}

char* getenv(const char* name)
{
    if (!name) return NULL;
    size_t vstart;
    if (!env_find(name, &vstart)) return NULL;
    const char *eq = strchr(&g_env_buf[vstart], '=');
    if (!eq) return NULL;
    return (char*)(eq + 1);
}

int setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !value || strlen(name) == 0 || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    size_t vstart;
    if (env_find(name, &vstart)) {
        if (!overwrite) return 0;
        const char *eq = strchr(&g_env_buf[vstart], '=');
        if (!eq) return -1;
        size_t name_len = (size_t)(eq - &g_env_buf[vstart]);
        size_t old_val_len = strlen(eq + 1);
        size_t new_val_len = strlen(value);
        if (new_val_len <= old_val_len) {
            memcpy((char*)(eq + 1), value, new_val_len + 1);
            return 0;
        }
        size_t tail_start = vstart + name_len + 1 + old_val_len + 1;
        size_t tail_len = g_env_len - tail_start;
        size_t new_entry_len = name_len + 1 + new_val_len + 1;
        size_t new_total = vstart + new_entry_len + tail_len;
        if (new_total > sizeof(g_env_buf)) { errno = ENOMEM; return -1; }
        memmove(&g_env_buf[vstart + new_entry_len], &g_env_buf[tail_start], tail_len);
        memcpy(&g_env_buf[vstart], name, name_len);
        g_env_buf[vstart + name_len] = '=';
        memcpy(&g_env_buf[vstart + name_len + 1], value, new_val_len + 1);
        g_env_len = new_total;
        return 0;
    }
    size_t name_len = strlen(name);
    size_t val_len = strlen(value);
    size_t entry_len = name_len + 1 + val_len + 1;
    if (g_env_len + entry_len > sizeof(g_env_buf)) { errno = ENOMEM; return -1; }
    memcpy(&g_env_buf[g_env_len], name, name_len);
    g_env_buf[g_env_len + name_len] = '=';
    memcpy(&g_env_buf[g_env_len + name_len + 1], value, val_len + 1);
    g_env_len += entry_len;
    return 0;
}

int unsetenv(const char *name)
{
    if (!name || strlen(name) == 0 || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    size_t vstart;
    if (env_find(name, &vstart)) {
        const char *eq = strchr(&g_env_buf[vstart], '=');
        if (!eq) return -1;
        size_t entry_len = (size_t)(eq - &g_env_buf[vstart]) + 1 + strlen(eq + 1) + 1;
        memmove(&g_env_buf[vstart], &g_env_buf[vstart + entry_len], g_env_len - vstart - entry_len);
        g_env_len -= entry_len;
    }
    return 0;
}

int putenv(char *string)
{
    if (!string || !strchr(string, '=')) {
        errno = EINVAL;
        return -1;
    }
    const char *eq = strchr(string, '=');
    size_t name_len = (size_t)(eq - string);
    char name_buf[256];
    if (name_len >= sizeof(name_buf)) { errno = EINVAL; return -1; }
    memcpy(name_buf, string, name_len);
    name_buf[name_len] = '\0';
    return setenv(name_buf, eq + 1, 1);
}

static unsigned int g_rand_state = 1;

int rand(void)
{
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return (int)((g_rand_state >> 16) & 0x7FFFu);
}

void srand(unsigned int seed)
{
    g_rand_state = seed ? seed : 1u;
}

int system(const char *command)
{
    if (!command) return 1;
    (void)command;
    errno = ENOSYS;
    return -1;
}
