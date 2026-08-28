#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) \
     ? (void)0 \
     : __assert_fail(#expr, __FILE__, __LINE__, __PRETTY_FUNCTION__))
#endif

#if !defined(__cplusplus) && !defined(static_assert)
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define static_assert _Static_assert
#else
#define static_assert(cond, msg) \
    typedef char __static_assert_##__LINE__[(cond) ? 1 : -1] \
        __attribute__((unused))
#endif
#endif

#ifdef __cplusplus
}
#endif
