#include "Salt.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void crypto_generate_salt(uint8_t salt[8])
{
    static int seeded = 0;
    if (!seeded) {
        uintptr_t seed = (uintptr_t)&seeded ^ (uintptr_t)salt;
        srand((unsigned int)((seed >> 32) ^ seed));
        seeded = 1;
    }

    for (size_t i = 0; i < 8; ++i) {
        salt[i] = (uint8_t)(rand() & 0xFF);
    }
}
