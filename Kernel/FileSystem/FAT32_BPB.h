#pragma once

#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint8_t  _reserved0;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint8_t  _reserved1;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t total_sectors;
} FAT32_BPB;
#pragma pack(pop)
