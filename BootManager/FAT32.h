#ifndef BOOTMANAGER_FAT32_H
#define BOOTMANAGER_FAT32_H

#include <efi.h>
#include <stdint.h>
#include "../Kernel/FileSystem/FAT32_BPB.h"

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
