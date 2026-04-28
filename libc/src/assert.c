#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KERNEL
extern void panic(const char* msg);
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    char buf[512];
    snprintf(buf, sizeof(buf), "Assertion failed: %s, file %s, line %u, function %s\n", assertion, file, line, function);
#ifdef KERNEL
    panic(buf);
#else
    printf("%s", buf);
    exit(1);
#endif
}
