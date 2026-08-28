#pragma once

#include <stddef.h>
#include <sys/types.h>

#define GRND_NONBLOCK 0x0001u
#define GRND_RANDOM   0x0002u

ssize_t getrandom(void *buffer, size_t length, unsigned int flags);
