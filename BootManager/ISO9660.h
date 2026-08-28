#ifndef IMPLUSOS_BOOTMANAGER_ISO9660_H
#define IMPLUSOS_BOOTMANAGER_ISO9660_H

#include <stdint.h>
#include <stddef.h>

#pragma pack(push, 1)

typedef struct {
    uint8_t length;
    uint8_t ext_attr_length;
    uint32_t extent_lba_le;
    uint32_t extent_lba_be;
    uint32_t data_length_le;
    uint32_t data_length_be;
    uint8_t recording_time[7];
    uint8_t flags;
    uint8_t file_unit_size;
    uint8_t interleave_gap_size;
    uint16_t vol_seq_num_le;
    uint16_t vol_seq_num_be;
    uint8_t name_length;
    char name[1];
} ISO9660_DIR_RECORD;

typedef struct {
    uint8_t type;
    char identifier[5];
    uint8_t version;
    uint8_t unused1;
    char system_identifier[32];
    char volume_identifier[32];
    uint8_t unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_seq_num_le;
    uint16_t volume_seq_num_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t path_table_lba_le;
    uint32_t path_table_optional_lba_le;
    uint32_t path_table_lba_be;
    uint32_t path_table_optional_lba_be;
    ISO9660_DIR_RECORD root_dir_record;
} ISO9660_PVD;

#pragma pack(pop)

typedef struct {
    uint32_t root_lba;
    uint32_t root_size;
    uint8_t boot_drive;
} ISO9660_FS;

#endif
