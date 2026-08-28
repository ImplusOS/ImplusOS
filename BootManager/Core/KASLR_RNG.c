#include "KASLR_RNG.h"

static inline uint64_t rdtsc_read(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t low, high;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
#else
    return 0x5A5A5A5A5A5A5A5AULL;
#endif
}

static inline int rdrand64_read(uint64_t *val)
{
#if defined(__x86_64__) || defined(_M_X64)
    uint32_t eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ __volatile__("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(1));
    if (!(ecx & (1u << 30))) {
        return 0;
    }

    unsigned char ok = 0;
    for (int retry = 0; retry < 10; ++retry) {
        __asm__ __volatile__("rdrand %0; setc %1" : "=r"(*val), "=q"(ok));
        if (ok) return 1;
    }
    return 0;
#else
    (void)val;
    return 0;
#endif
}

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t kaslr_get_random64(void)
{
    static uint64_t state = 0x123456789ABCDEF0ULL;
    uint64_t rdrand_val = 0;
    uint64_t tsc_val = rdtsc_read();

    state ^= tsc_val;

    if (rdrand64_read(&rdrand_val)) {
        state ^= rdrand_val;
    }

    return splitmix64(&state);
}

uint64_t kaslr_calculate_slide(uint64_t max_slides, uint64_t alignment)
{
    return kaslr_calculate_slide_range(0, max_slides, alignment);
}

uint64_t kaslr_calculate_slide_range(uint64_t min_slide, uint64_t max_slides, uint64_t alignment)
{
    if (alignment == 0) {
        return min_slide;
    }
    if (max_slides == 0) {
        return min_slide;
    }
    uint64_t rand_val = kaslr_get_random64();
    uint64_t slot = rand_val % max_slides;
    return min_slide + (slot * alignment);
}
