#include "Salt.h"
#include <stddef.h>
#include <stdint.h>

extern int32_t process_get_current_pid(void);

#ifdef KERNEL
extern uint64_t timer_ticks(void);
#else
extern uint64_t get_uptime_ms(void);
#endif

static uint64_t g_salt_prng_state;

static uint64_t salt_time_seed(void)
{
#ifdef KERNEL
    return timer_ticks();
#else
    return get_uptime_ms();
#endif
}

static uint64_t salt_prng_next(void)
{
    uint64_t x = g_salt_prng_state;
    if (x == 0u) {
        x = salt_time_seed() ^
            ((uint64_t)(uint32_t)process_get_current_pid() << 32) ^
            (uint64_t)(uintptr_t)&g_salt_prng_state ^
            0x9e3779b97f4a7c15ULL;
    }
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_salt_prng_state = x;
    return x * 0x2545f4914f6cdd1dULL;
}

void crypto_generate_salt_bytes(uint8_t *salt, size_t length)
{
    if (salt == NULL) {
        return;
    }
    for (size_t i = 0; i < length; ++i) {
        if ((i & 7u) == 0u) {
            (void)salt_prng_next();
        }
        salt[i] = (uint8_t)(g_salt_prng_state >> ((i & 7u) * 8u));
    }
}

void crypto_generate_salt(uint8_t salt[8])
{
    crypto_generate_salt_bytes(salt, 8u);
}
