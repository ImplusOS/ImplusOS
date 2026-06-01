#pragma once

#include <stdbool.h>
#include <stdint.h>

bool block_manager_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool block_manager_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
