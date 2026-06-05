#ifndef BOOTMANAGER_FAT32_H
#define BOOTMANAGER_FAT32_H

#include <Uefi.h>
#include <Protocol/BlockIo.h>

#pragma pack(push, 1)
typedef struct {
    UINT16  bytes_per_sector;
    UINT8   sectors_per_cluster;
    UINT8   reserved0;
    UINT16  reserved_sectors;
    UINT8   num_fats;
    UINT8   reserved1;
    UINT32  fat_size_sectors;
    UINT32  root_cluster;
    UINT32  total_sectors;
} FAT32_BPB;
#pragma pack(pop)

typedef struct {
    EFI_SYSTEM_TABLE        *SystemTable;
    EFI_BLOCK_IO_PROTOCOL    *BlockIo;
    FAT32_BPB                BPB;
    UINT64                   PartitionStartLBA;
    UINT32                   FirstDataSector;
    UINTN                    BytesPerCluster;
} BOOTMANAGER_FAT32;

EFI_STATUS
bootmanager_fat32_init(
    BOOTMANAGER_FAT32   *fs,
    EFI_SYSTEM_TABLE    *system_table,
    EFI_HANDLE           device_handle,
    UINT64               partition_start_lba,
    CONST FAT32_BPB     *bpb
);

EFI_STATUS
bootmanager_fat32_read_file(
    BOOTMANAGER_FAT32   *fs,
    CONST CHAR8         *path,
    VOID               **buffer,
    UINTN               *size
);

#endif