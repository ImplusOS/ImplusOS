#ifndef BOOTMANAGER_BOOT_INFO_H
#define BOOTMANAGER_BOOT_INFO_H

/*
 * Shared BOOT_INFO definition for both UEFI and BIOS BootManagers.
 *
 * This header defines the canonical BOOT_INFO struct that both the UEFI
 * BootManager (BootManager.c) and the BIOS BootManager (BIOS/BootManager_BIOS.c)
 * must fill before handing off to the kernel.  The struct layout matches
 * Kernel/include/kernel/boot_info.h exactly.
 *
 * Uses only stdint.h types so it is portable across the 64-bit UEFI and
 * 32-bit BIOS build environments.
 */

#include <stdint.h>
#include <stddef.h>
#include "../Kernel/FileSystem/FAT32_BPB.h"

/* ------------------------------------------------------------------ */
/* Portable type aliases                                               */
/* In the UEFI build, <efi.h> is included first and already provides   */
/* UINTN / UINT32 / EFI_PHYSICAL_ADDRESS / EFI_MEMORY_DESCRIPTOR.      */
/* For the BIOS build those headers are not available, so we provide    */
/* compatible definitions here.                                        */
/* ------------------------------------------------------------------ */
#ifndef _EFI_INCLUDE_

typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint64_t EFI_PHYSICAL_ADDRESS;

typedef struct {
    UINT32   Type;
    UINT32   Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

#endif /* !_EFI_INCLUDE_ */

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */
#define MAX_LOADED_FILES     16
#define LOADED_FILE_NAME_MAX 64

#define BOOT_DRIVE_TYPE_UNKNOWN 0
#define BOOT_DRIVE_TYPE_IDE     1
#define BOOT_DRIVE_TYPE_USB     2

#define EFI_RESERVED_MEMORY_TYPE 0u
#define EFI_CONVENTIONAL_MEMORY  7u

/* ------------------------------------------------------------------ */
/* LOADED_FILE                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    char                 Name[LOADED_FILE_NAME_MAX];
    EFI_PHYSICAL_ADDRESS PhysAddr;
    uint64_t             Size;
} LOADED_FILE;

/* ------------------------------------------------------------------ */
/* BOOT_INFO — the structure passed to kernel_main()                   */
/* ------------------------------------------------------------------ */
typedef struct {
    EFI_MEMORY_DESCRIPTOR *MemoryMap;
    UINTN                  MemoryMapSize;
    UINTN                  MemoryMapDescriptorSize;
    UINT32                 MemoryMapDescriptorVersion;

    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    UINT32   HorizontalResolution;
    UINT32   VerticalResolution;
    UINT32   PixelsPerScanLine;

    uint64_t  PartitionStartLBA;
    FAT32_BPB BootPartitionBPB;
    UINT32    BootPartitionBPBValid;

    uint64_t AcpiRsdpAddress;
    UINT32   AcpiRsdpSize;
    UINT32   AcpiRsdpRevision;

    UINT32   BootDriveType;

    uint64_t RamDiskBase;
    uint64_t RamDiskSize;
    UINT32   RamDiskSectorSize;
    UINT32   RamDiskReserved;

    UINTN       LoadedFileCount;
    LOADED_FILE LoadedFiles[MAX_LOADED_FILES];

    uint64_t FontDataAddress;
    uint64_t FontDataSize;
} BOOT_INFO;

#endif /* BOOTMANAGER_BOOT_INFO_H */
