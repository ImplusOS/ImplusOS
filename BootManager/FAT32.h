#ifndef BOOTMANAGER_FAT32_H
#define BOOTMANAGER_FAT32_H

#include <efi.h>
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

typedef struct {
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_BLOCK_IO_PROTOCOL *BlockIo;
    FAT32_BPB BPB;
    uint64_t PartitionStartLBA;
    uint32_t FirstDataSector;
    uint32_t BytesPerCluster;
} BOOTMANAGER_FAT32;

EFI_STATUS bootmanager_fat32_init(
    BOOTMANAGER_FAT32 *fs,
    EFI_SYSTEM_TABLE *system_table,
    EFI_HANDLE device_handle,
    uint64_t partition_start_lba,
    const FAT32_BPB *bpb
);

EFI_STATUS bootmanager_fat32_read_file(
    BOOTMANAGER_FAT32 *fs,
    const char *path,
    void **buffer,
    UINTN *size
);

#endif
