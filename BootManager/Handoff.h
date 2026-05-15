#ifndef IMPLUSOS_BOOT_HANDOFF_H
#define IMPLUSOS_BOOT_HANDOFF_H

#include <efi.h>
#include <stdint.h>
#include "../Kernel/FileSystem/FAT32_BPB.h"

#define IMPLUSOS_BOOT_HANDOFF_GUID \
    { 0x6f1a3bb7, 0x7ad5, 0x4fa2, {0x8e, 0x90, 0x42, 0xb8, 0x11, 0x8d, 0x38, 0x51} }

#define BOOT_DRIVE_TYPE_UNKNOWN 0
#define BOOT_DRIVE_TYPE_IDE     1
#define BOOT_DRIVE_TYPE_USB     2

typedef struct {
    uint32_t Signature;
    uint32_t Version;

    uint64_t PartitionStartLBA;
    FAT32_BPB BootPartitionBPB;
    uint32_t BootPartitionBPBValid;
    uint32_t BootDriveType;

    uint64_t AcpiRsdpAddress;
    uint32_t AcpiRsdpSize;
    uint32_t AcpiRsdpRevision;

    char CPUName[64];
    char Manufacturer[64];
    char ProductName[64];
} BOOTLOADER_HANDOFF;

#define IMPLUSOS_BOOT_HANDOFF_SIGNATURE 0x48424d49u
#define IMPLUSOS_BOOT_HANDOFF_VERSION   1u

#endif
