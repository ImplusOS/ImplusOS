#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "../../../libc/include/string.h"
#include "../../../BootManager/Handoff.h"

#ifndef ACPI_TABLE_GUID
#define ACPI_TABLE_GUID \
    { 0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }
#endif

#ifndef ACPI_20_TABLE_GUID
#define ACPI_20_TABLE_GUID \
    { 0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81} }
#endif

#ifndef SMBIOS_TABLE_GUID
#define SMBIOS_TABLE_GUID \
    { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }
#endif

#ifndef SMBIOS3_TABLE_GUID
#define SMBIOS3_TABLE_GUID \
    { 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} }
#endif

#define FAT32_BOOT_BLOCK_MAX 4096u
#define ISO_TO_ATA(iso_lba) ((UINT64)(iso_lba) * 4ULL)

typedef struct {
    char     Signature[8];
    uint8_t  Checksum;
    char     OEMID[6];
    uint8_t  Revision;
    uint32_t RsdtAddress;
} __attribute__((packed)) ACPI_RSDP_V1;

typedef struct {
    ACPI_RSDP_V1 V1;
    uint32_t     Length;
    uint64_t     XsdtAddress;
    uint8_t      ExtendedChecksum;
    uint8_t      Reserved[3];
} __attribute__((packed)) ACPI_RSDP_V2;

#pragma pack(push, 1)
typedef struct {
    UINT8  AnchorString[4];
    UINT8  EntryPointStructureChecksum;
    UINT8  EntryPointLength;
    UINT8  SMBIOSMajorVersion;
    UINT8  SMBIOSMinorVersion;
    UINT16 MaxStructureSize;
    UINT8  EntryPointRevision;
    UINT8  FormattedArea[5];
    UINT8  IntermediateAnchorString[5];
    UINT8  IntermediateChecksum;
    UINT16 StructureTableLength;
    UINT32 StructureTableAddress;
    UINT16 NumberOfSMBIOSStructures;
    UINT8  SMBIOSBCDRevision;
} SMBIOS_EPS;

typedef struct {
    UINT8  AnchorString[5];
    UINT8  EntryPointStructureChecksum;
    UINT8  EntryPointLength;
    UINT8  SMBIOSMajorVersion;
    UINT8  SMBIOSMinorVersion;
    UINT8  SMBIOSDocrev;
    UINT8  EntryPointRevision;
    UINT8  Reserved;
    UINT32 StructureTableMaximumSize;
    UINT64 StructureTableAddress;
} SMBIOS3_EPS;

typedef struct {
    UINT8  Type;
    UINT8  Length;
    UINT16 Handle;
} UEFI_SMBIOS_HEADER;
#pragma pack(pop)

static UINT16 ReadLe16(const UINT8 *p) {
    return p ? ((UINT16)p[0] | ((UINT16)p[1] << 8)) : 0;
}

static UINT32 ReadLe32(const UINT8 *p) {
    return p ? ((UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24)) : 0;
}

static BOOLEAN GuidsAreEqual(EFI_GUID *g1, EFI_GUID *g2) {
    UINT8 *b1 = (UINT8 *)g1;
    UINT8 *b2 = (UINT8 *)g2;
    for (int i = 0; i < 16; ++i) {
        if (b1[i] != b2[i]) return FALSE;
    }
    return TRUE;
}

static void TrimString(char *str) {
    if (!str || !*str) return;
    char *start = str;
    while (*start == ' ') ++start;
    if (start != str) {
        char *dst = str;
        while (*start) *dst++ = *start++;
        *dst = 0;
    }
    UINTN len = 0;
    while (str[len]) ++len;
    while (len > 0 && str[len - 1] == ' ') str[--len] = 0;
}

static char *GetSmbiosString(UEFI_SMBIOS_HEADER *hdr, UINT8 index, UINT8 *end) {
    if (index == 0) return NULL;
    char *str = (char *)hdr + hdr->Length;
    for (UINT8 i = 1; i < index; ++i) {
        while ((UINT8 *)str < end && *str != 0) ++str;
        ++str;
        if ((UINT8 *)str >= end || *str == 0) return NULL;
    }
    return ((UINT8 *)str < end) ? str : NULL;
}

static void CopySmbiosString(char *dst, char *src, UINT8 *end) {
    if (!dst || dst[0] || !src) return;
    UINTN len = 0;
    while (src[len] && len < 63 && (UINT8 *)&src[len] < end) ++len;
    memcpy(dst, src, len);
    dst[len] = 0;
    TrimString(dst);
}

static void DiscoverSMBIOS(EFI_SYSTEM_TABLE *ST, BOOTLOADER_HANDOFF *Handoff) {
    EFI_GUID smbios_guid = SMBIOS_TABLE_GUID;
    EFI_GUID smbios3_guid = SMBIOS3_TABLE_GUID;
    void *smbios = NULL;
    BOOLEAN is_smbios3 = FALSE;

    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        if (GuidsAreEqual(&ST->ConfigurationTable[i].VendorGuid, &smbios3_guid)) {
            smbios = ST->ConfigurationTable[i].VendorTable;
            is_smbios3 = TRUE;
            break;
        }
        if (GuidsAreEqual(&ST->ConfigurationTable[i].VendorGuid, &smbios_guid)) {
            smbios = ST->ConfigurationTable[i].VendorTable;
        }
    }
    if (!smbios) return;

    UINT8 *table_addr = NULL;
    UINT32 table_len = 0;
    UINT32 num_structs = 0;
    if (is_smbios3) {
        SMBIOS3_EPS *eps3 = (SMBIOS3_EPS *)smbios;
        table_addr = (UINT8 *)(UINTN)eps3->StructureTableAddress;
        table_len = eps3->StructureTableMaximumSize;
        num_structs = 0xFFFFFFFFu;
    } else {
        SMBIOS_EPS *eps = (SMBIOS_EPS *)smbios;
        table_addr = (UINT8 *)(UINTN)eps->StructureTableAddress;
        table_len = eps->StructureTableLength;
        num_structs = eps->NumberOfSMBIOSStructures;
    }
    if (!table_addr || table_len == 0) return;

    UINT8 *ptr = table_addr;
    UINT8 *end = table_addr + table_len;
    for (UINT32 count = 0; ptr + sizeof(UEFI_SMBIOS_HEADER) <= end && count < num_structs; ++count) {
        UEFI_SMBIOS_HEADER *hdr = (UEFI_SMBIOS_HEADER *)ptr;
        if (hdr->Type == 127 || hdr->Length < sizeof(UEFI_SMBIOS_HEADER)) break;
        if (hdr->Type == 1 && hdr->Length >= 6) {
            CopySmbiosString(Handoff->Manufacturer, GetSmbiosString(hdr, ptr[0x04], end), end);
            CopySmbiosString(Handoff->ProductName, GetSmbiosString(hdr, ptr[0x05], end), end);
        } else if (hdr->Type == 4 && hdr->Length >= 0x11) {
            CopySmbiosString(Handoff->CPUName, GetSmbiosString(hdr, ptr[0x10], end), end);
        }
        ptr += hdr->Length;
        while (ptr + 1 < end) {
            if (ptr[0] == 0 && ptr[1] == 0) {
                ptr += 2;
                break;
            }
            ++ptr;
        }
    }
}

static void DiscoverAcpiRsdp(EFI_SYSTEM_TABLE *ST, BOOTLOADER_HANDOFF *Handoff) {
    EFI_GUID acpi10 = ACPI_TABLE_GUID;
    EFI_GUID acpi20 = ACPI_20_TABLE_GUID;

    for (int pass = 0; pass < 2; ++pass) {
        EFI_GUID *target = (pass == 0) ? &acpi20 : &acpi10;
        for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
            EFI_CONFIGURATION_TABLE *cfg = &ST->ConfigurationTable[i];
            if (!cfg->VendorTable || !GuidsAreEqual(&cfg->VendorGuid, target)) continue;
            const CHAR8 *sig = (const CHAR8 *)cfg->VendorTable;
            if (sig[0] != 'R' || sig[1] != 'S' || sig[2] != 'D' || sig[3] != ' ' ||
                sig[4] != 'P' || sig[5] != 'T' || sig[6] != 'R' || sig[7] != ' ') continue;
            ACPI_RSDP_V1 *v1 = (ACPI_RSDP_V1 *)cfg->VendorTable;
            Handoff->AcpiRsdpAddress = (uint64_t)(UINTN)cfg->VendorTable;
            Handoff->AcpiRsdpRevision = v1->Revision;
            Handoff->AcpiRsdpSize = (UINT32)sizeof(ACPI_RSDP_V1);
            if (v1->Revision >= 2) {
                ACPI_RSDP_V2 *v2 = (ACPI_RSDP_V2 *)cfg->VendorTable;
                if (v2->Length > 0) Handoff->AcpiRsdpSize = v2->Length;
            }
            return;
        }
    }
}

static void ParseBootSectorBPB(const UINT8 *sector, FAT32_BPB *out_bpb) {
    UINT16 total16 = ReadLe16(&sector[19]);
    UINT32 total32 = ReadLe32(&sector[32]);
    out_bpb->bytes_per_sector = ReadLe16(&sector[11]);
    out_bpb->sectors_per_cluster = sector[13];
    out_bpb->reserved_sectors = ReadLe16(&sector[14]);
    out_bpb->num_fats = sector[16];
    out_bpb->fat_size_sectors = ReadLe32(&sector[36]);
    out_bpb->root_cluster = ReadLe32(&sector[44]);
    out_bpb->total_sectors = (total16 != 0) ? (UINT32)total16 : total32;
}

static void CaptureBootPartitionBPB(EFI_HANDLE DeviceHandle, EFI_SYSTEM_TABLE *ST,
                                    BOOTLOADER_HANDOFF *Handoff, UINT64 PartitionStartLBA) {
    EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
    if (EFI_ERROR(Status) || !Bio || !Bio->Media) return;
    UINTN block_size = Bio->Media->BlockSize;
    if (block_size < 512u || block_size > FAT32_BOOT_BLOCK_MAX) return;
    UINT8 *block = NULL;
    Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, block_size, (VOID **)&block);
    if (EFI_ERROR(Status) || !block) return;
    // ↓ LBA 0 → PartitionStartLBA に変更
    Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                               PartitionStartLBA, block_size, block);
    if (!EFI_ERROR(Status) && block[510] == 0x55 && block[511] == 0xAA) {
        ParseBootSectorBPB(block, &Handoff->BootPartitionBPB);
        Handoff->BootPartitionBPBValid = 1;
    }
    uefi_call_wrapper(ST->BootServices->FreePool, 1, block);
}

static UINT64 ParseElToritoCatalog(EFI_BLOCK_IO_PROTOCOL *Bio, EFI_SYSTEM_TABLE *ST) {
    UINT32 BS = Bio->Media->BlockSize;
    if (BS < 512 || BS > 4096) return 0;
    UINT8 *Buf = NULL;
    EFI_STATUS Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, BS, (VOID **)&Buf);
    if (EFI_ERROR(Status)) return 0;
    UINT64 Result = 0;
    UINT64 Lba16 = (16ULL * 2048ULL) / (UINT64)BS;
    Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba16, BS, Buf);
    if (!EFI_ERROR(Status) && Buf[1] == 'C' && Buf[2] == 'D' && Buf[3] == '0' && Buf[4] == '0' && Buf[5] == '1') {
        for (UINT32 S = 17; S < 32 && !Result; ++S) {
            UINT64 Lba = ((UINT64)S * 2048ULL) / (UINT64)BS;
            Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba, BS, Buf);
            if (EFI_ERROR(Status) || Buf[0] == 0xFF || Buf[0] != 0x00) continue;
            UINT32 CatIsoLBA = (UINT32)Buf[71] | ((UINT32)Buf[72] << 8) |
                               ((UINT32)Buf[73] << 16) | ((UINT32)Buf[74] << 24);
            if (!CatIsoLBA) continue;
            UINT64 CatBlock = ((UINT64)CatIsoLBA * 2048ULL) / (UINT64)BS;
            Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, CatBlock, BS, Buf);
            if (EFI_ERROR(Status)) continue;
            UINT8 *E = Buf + 32;
            if (E[0] == 0x88) {
                UINT32 RBA = (UINT32)E[8] | ((UINT32)E[9] << 8) |
                             ((UINT32)E[10] << 16) | ((UINT32)E[11] << 24);
                if (RBA) Result = ISO_TO_ATA(RBA);
            }
        }
    }
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Buf);
    return Result;
}

static UINT64 GetPartitionStartLBA(EFI_HANDLE DeviceHandle, EFI_SYSTEM_TABLE *ST, BOOTLOADER_HANDOFF *Handoff) {
    Handoff->BootDriveType = BOOT_DRIVE_TYPE_UNKNOWN;
    EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);
    if (!EFI_ERROR(Status) && DevicePath) {
        EFI_DEVICE_PATH_PROTOCOL *Node = DevicePath;
        while (!IsDevicePathEnd(Node)) {
            if (DevicePathType(Node) == 3) {
                if (DevicePathSubType(Node) == 5) Handoff->BootDriveType = BOOT_DRIVE_TYPE_USB;
                else if (DevicePathSubType(Node) == 1 || DevicePathSubType(Node) == 18) Handoff->BootDriveType = BOOT_DRIVE_TYPE_IDE;
            }
            if (DevicePathType(Node) == 4 && DevicePathSubType(Node) == 1) {
                HARDDRIVE_DEVICE_PATH *HD = (HARDDRIVE_DEVICE_PATH *)Node;
                return HD->PartitionStart;
            }
            Node = NextDevicePathNode(Node);
        }
    }

    UINTN Count = 0;
    EFI_HANDLE *Handles = NULL;
    Status = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR(Status) && Handles) {
        for (UINTN i = 0; i < Count; ++i) {
            EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
            uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
                Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
            if (!Bio || Bio->Media->LogicalPartition || Bio->Media->LastBlock < 200) continue;
            UINT64 Lba = ParseElToritoCatalog(Bio, ST);
            if (Lba) {
                uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
                return Lba;
            }
        }
        uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
    }
    return 2048ULL;
}

static EFI_FILE_PROTOCOL *OpenFsRootFromHandle(EFI_HANDLE Handle, EFI_SYSTEM_TABLE *ST) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR(Status) || !Fs) return NULL;
    Status = uefi_call_wrapper(Fs->OpenVolume, 2, Fs, &Root);
    return EFI_ERROR(Status) ? NULL : Root;
}

static EFI_STATUS TryStartBootManagerFromHandle(
    EFI_HANDLE ImageHandle,
    EFI_SYSTEM_TABLE *ST,
    EFI_HANDLE DeviceHandle
) {
    EFI_FILE_PROTOCOL *Root = OpenFsRootFromHandle(DeviceHandle, ST);
    if (!Root) return EFI_NOT_FOUND;

    EFI_FILE_PROTOCOL *File = NULL;
    EFI_STATUS Status = uefi_call_wrapper(Root->Open, 5, Root, &File,
        L"EFI\\BOOT\\BOOTMANAGER.EFI", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(Root->Close, 1, Root);
        return Status;
    }
    uefi_call_wrapper(File->Close, 1, File);
    uefi_call_wrapper(Root->Close, 1, Root);

    EFI_DEVICE_PATH *BootManagerPath = FileDevicePath(
        DeviceHandle, L"\\EFI\\BOOT\\BOOTMANAGER.EFI");
    if (!BootManagerPath) return EFI_OUT_OF_RESOURCES;

    EFI_HANDLE BootManagerHandle = NULL;
    Status = uefi_call_wrapper(ST->BootServices->LoadImage, 6,
        FALSE, ImageHandle, BootManagerPath, NULL, 0, &BootManagerHandle);
    uefi_call_wrapper(ST->BootServices->FreePool, 1, BootManagerPath);
    if (EFI_ERROR(Status)) return Status;

    UINTN ExitDataSize = 0;
    CHAR16 *ExitData = NULL;
    return uefi_call_wrapper(ST->BootServices->StartImage, 3,
        BootManagerHandle, &ExitDataSize, &ExitData);
}

static EFI_STATUS StartBootManager(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage) return Status;

    Status = TryStartBootManagerFromHandle(ImageHandle, ST, LoadedImage->DeviceHandle);
    if (!EFI_ERROR(Status)) return EFI_SUCCESS;

    UINTN Count = 0;
    EFI_HANDLE *Handles = NULL;
    Status = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || !Handles) return Status;

    EFI_STATUS LastStatus = EFI_NOT_FOUND;
    for (UINTN i = 0; i < Count; ++i) {
        if (Handles[i] == LoadedImage->DeviceHandle) continue;
        Status = TryStartBootManagerFromHandle(ImageHandle, ST, Handles[i]);
        if (!EFI_ERROR(Status)) {
            uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
            return EFI_SUCCESS;
        }
        LastStatus = Status;
    }
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
    return LastStatus;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    InitializeLib(ImageHandle, ST);

    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage) return Status;

    BOOTLOADER_HANDOFF *Handoff = NULL;
    Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
        EfiLoaderData, sizeof(BOOTLOADER_HANDOFF), (VOID **)&Handoff);
    if (EFI_ERROR(Status) || !Handoff) return Status;
    memset(Handoff, 0, sizeof(*Handoff));
    Handoff->Signature = IMPLUSOS_BOOT_HANDOFF_SIGNATURE;
    Handoff->Version = IMPLUSOS_BOOT_HANDOFF_VERSION;

    DiscoverSMBIOS(ST, Handoff);
    DiscoverAcpiRsdp(ST, Handoff);
    Handoff->PartitionStartLBA = GetPartitionStartLBA(LoadedImage->DeviceHandle, ST, Handoff);
    CaptureBootPartitionBPB(LoadedImage->DeviceHandle, ST, Handoff, Handoff->PartitionStartLBA); // ← LBAを渡す

    EFI_GUID handoff_guid = IMPLUSOS_BOOT_HANDOFF_GUID;
    Status = uefi_call_wrapper(ST->BootServices->InstallConfigurationTable, 2, &handoff_guid, Handoff);
    if (EFI_ERROR(Status)) return Status;

    Status = StartBootManager(ImageHandle, ST);
    if (EFI_ERROR(Status)) return Status;

    return EFI_SUCCESS;
}
