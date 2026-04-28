#pragma once
#include <stdbool.h>
#include <stdint.h>

bool usb_ms_init(uint64_t partition_lba);
bool usb_ms_read(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool usb_ms_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
bool usb_ms_is_working(void);
