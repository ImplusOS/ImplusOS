#pragma once
#include <stdbool.h>
#include <stdint.h>

bool ata_init(uint64_t partition_lba);
bool ata_read(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool ata_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
bool ata_is_working(void);
