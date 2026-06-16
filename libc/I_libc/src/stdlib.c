#include <stdlib.h>
#include <stdint.h>

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

static void qsort_swap(unsigned char* a, unsigned char* b, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        unsigned char tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    unsigned char* bytes = (unsigned char*)base;
    if (!base || !compar || size == 0 || nmemb < 2) {
        return;
    }
    for (size_t i = 0; i < nmemb; ++i) {
        for (size_t j = i + 1; j < nmemb; ++j) {
            if (compar(bytes + i * size, bytes + j * size) > 0) {
                qsort_swap(bytes + i * size, bytes + j * size, size);
            }
        }
    }
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

char* getenv(const char* name)
{
    (void)name;
    return NULL;
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
