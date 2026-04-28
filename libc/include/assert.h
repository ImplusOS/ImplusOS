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

#ifdef __cplusplus
}
#endif
