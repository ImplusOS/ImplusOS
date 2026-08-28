#ifndef IMPLUSOS_BOOT_KASLR_RNG_H
#define IMPLUSOS_BOOT_KASLR_RNG_H

#include <stdint.h>
#include <stddef.h>

uint64_t kaslr_get_random64(void);
uint64_t kaslr_calculate_slide(uint64_t max_slides, uint64_t alignment);
uint64_t kaslr_calculate_slide_range(uint64_t min_slide, uint64_t max_slides, uint64_t alignment);

#endif
