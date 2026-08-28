#ifndef _FENV_H
#define _FENV_H

#include <stdint.h>

typedef uint32_t fenv_t;
typedef uint32_t fexcept_t;

#define FE_TONEAREST  0
#define FE_DOWNWARD   1
#define FE_UPWARD     2
#define FE_TOWARDZERO 3

#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20
#define FE_ALL_EXCEPT (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

static inline int fegetround(void)
{
#if defined(PLATFORM_X86_64)
    uint32_t mxcsr;
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    switch ((mxcsr >> 13) & 0x3u) {
        case 1:  return FE_DOWNWARD;
        case 2:  return FE_UPWARD;
        case 3:  return FE_TOWARDZERO;
        default: return FE_TONEAREST;
    }
#elif defined(PLATFORM_ARM64)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    switch ((fpcr >> 22) & 0x3u) {
        case 1:  return FE_UPWARD;
        case 2:  return FE_DOWNWARD;
        case 3:  return FE_TOWARDZERO;
        default: return FE_TONEAREST;
    }
#else
    return FE_TONEAREST;
#endif
}

static inline int fesetround(int round)
{
#if defined(PLATFORM_X86_64)
    uint32_t mxcsr, mode;
    switch (round) {
        case FE_DOWNWARD:   mode = 1u; break;
        case FE_UPWARD:     mode = 2u; break;
        case FE_TOWARDZERO: mode = 3u; break;
        default:            mode = 0u; break;
    }
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    mxcsr = (mxcsr & ~(0x3u << 13)) | (mode << 13);
    __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));
    return 0;
#elif defined(PLATFORM_ARM64)
    uint64_t fpcr, mode;
    switch (round) {
        case FE_UPWARD:     mode = 1u; break;
        case FE_DOWNWARD:   mode = 2u; break;
        case FE_TOWARDZERO: mode = 3u; break;
        default:            mode = 0u; break;
    }
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr = (fpcr & ~((uint64_t)0x3u << 22)) | (mode << 22);
    __asm__ volatile("msr fpcr, %0" :: "r"(fpcr));
    return 0;
#else
    (void)round;
    return 0;
#endif
}

static inline int feclearexcept(int excepts)          { (void)excepts; return 0; }
static inline int feraiseexcept(int excepts)           { (void)excepts; return 0; }
static inline int fetestexcept(int excepts)            { (void)excepts; return 0; }
static inline int fegetenv(fenv_t *envp)                { if (envp) *envp = 0u; return 0; }
static inline int fesetenv(const fenv_t *envp)          { (void)envp; return 0; }
static inline int feholdexcept(fenv_t *envp)            { if (envp) *envp = 0u; return 0; }
static inline int feupdateenv(const fenv_t *envp)       { (void)envp; return 0; }

#endif /* _FENV_H */