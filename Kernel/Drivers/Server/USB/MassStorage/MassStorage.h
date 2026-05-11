#pragma once
#include <stdint.h>
#include <stdbool.h>

bool bot_init(void);
bool bot_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool bot_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
