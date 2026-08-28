#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KERNEL
extern void kernel_panic(const char *module_name, const char *message);
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    char buf[512];
    snprintf(buf, sizeof(buf), "Assertion failed: %s, file %s, line %u, function %s\n", assertion, file, line, function);
#ifdef KERNEL
    kernel_panic("assert", buf);
#else
    printf("%s", buf);
    exit(1);
#endif
}
