#ifndef BOOTMANAGER_BOOT_INFO_H
#define BOOTMANAGER_BOOT_INFO_H

#include <stdint.h>
#include <stddef.h>

#ifndef BASE_LIBRARY_JUMP_BUFFER_ALIGNMENT

typedef unsigned long long UINTN;
typedef unsigned int       UINT32;
typedef unsigned long long EFI_PHYSICAL_ADDRESS;

#endif

#define MAX_LOADED_FILES     16
#define LOADED_FILE_NAME_MAX 64

#define BOOT_DRIVE_TYPE_UNKNOWN 0
#define BOOT_DRIVE_TYPE_IDE     1
#define BOOT_DRIVE_TYPE_USB     2
#define BOOT_DRIVE_TYPE_AHCI    3

#define EFI_RESERVED_MEMORY_TYPE 0u
#define EFI_CONVENTIONAL_MEMORY  7u

typedef struct {
    char                 Name[LOADED_FILE_NAME_MAX];
    EFI_PHYSICAL_ADDRESS PhysAddr;
    uint64_t             Size;
} LOADED_FILE;

#pragma pack(push, 1)
typedef struct {
    void    *MemoryMap;
    uint64_t MemoryMapSize;
    UINTN    MemoryMapDescriptorSize;
    uint32_t MemoryMapDescriptorVersion;

    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelsPerScanLine;

    uint64_t PartitionStartLBA;

    uint64_t AcpiRsdpAddress;
    uint32_t AcpiRsdpSize;
    uint32_t AcpiRsdpRevision;

    uint32_t BootDriveType;

    uint64_t RamDiskBase;
    uint64_t RamDiskSize;
    uint32_t RamDiskSectorSize;
    uint32_t RamDiskReserved;

    uint64_t    LoadedFileCount;
    LOADED_FILE LoadedFiles[MAX_LOADED_FILES];

    uint64_t FontDataAddress;
    uint64_t FontDataSize;
} BOOT_INFO;
#pragma pack(pop)

#endif