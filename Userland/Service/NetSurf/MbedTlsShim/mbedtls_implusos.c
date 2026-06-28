#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

#include <mbedtls/platform_time.h>

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                          size_t *olen)
{
    (void)data;

    if (olen == NULL || (output == NULL && len != 0)) {
        return -1;
    }

    *olen = 0;
    if (len == 0) {
        return 0;
    }

    if (getrandom(output, len, 0) != (ssize_t)len) {
        memset(output, 0, len);
        return -1;
    }

    *olen = len;
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (mbedtls_ms_time_t)time(NULL) * 1000;
    }

    return ((mbedtls_ms_time_t)ts.tv_sec * 1000) +
           ((mbedtls_ms_time_t)ts.tv_nsec / 1000000);
}
