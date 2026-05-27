#pragma once
#include <stdbool.h>
#include <stdint.h>

bool ahci_init(uint64_t partition_lba);
bool ahci_read(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool ahci_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
bool ahci_is_working(void);