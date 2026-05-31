#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include "../BootManager_libc/include/string.h"
#include "../BootManager_libc/include/stdlib.h"
#include "../Handoff.h"
#include "../Math.h"


#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STB_TRUETYPE_NO_STDIO
#define STBTT_assert(x)

#define STBTT_memcpy  memcpy
#define STBTT_memset  memset
#define STBTT_memmove memmove
#define STBTT_strlen  strlen

#define STBTT_sqrt(x)   stbtt_sqrt_impl(x)
#define STBTT_pow(x,y)  stbtt_pow_impl(x,y)
#define STBTT_fmod(x,y) stbtt_fmod_impl(x,y)
#define STBTT_cos(x)    stbtt_cos_impl(x)
#define STBTT_acos(x)   stbtt_acos_impl(x)
#define STBTT_fabs(x)   stbtt_fabs_impl(x)
#define STBTT_ifloor(x) stbtt_ifloor_impl(x)
#define STBTT_iceil(x)  stbtt_iceil_impl(x)

#define STBTT_malloc(x,u) malloc(x)
#define STBTT_free(x,u)   free(x)

#define STBTT_STATIC
#define STBTT__NOTUSED(v) (void)sizeof(v)
#define STB_TRUETYPE_NO_MATH

#include "../../Thirdparty/stb_truetype.h"

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

#include "../ISO9660.h"
#include "../BootInfo.h"
#include "../ElfDefs.h"

#define FAT32_BOOT_BLOCK_MAX 4096u

static char* strchr_local(const char* s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char*)s;
}

static char tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && (tolower(*s1) == tolower(*s2))) {
        s1++;
        s2++;
    }
    return (int)(unsigned char)tolower(*s1) - (int)(unsigned char)tolower(*s2);
}

static int StrEndsWith_ELF_8(const char *name) {
    size_t len = strlen(name);
    if (len < 4) return 0;
    return (name[len - 4] == '.' &&
            (name[len - 3] == 'E' || name[len - 3] == 'e') &&
            (name[len - 2] == 'L' || name[len - 2] == 'l') &&
            (name[len - 1] == 'F' || name[len - 1] == 'f'));
}

static void IsoNormalizeName(char *name) {
    char *Semicolon = strchr_local(name, ';');
    if (Semicolon) *Semicolon = 0;

    size_t Len = strlen(name);
    if (Len > 0 && name[Len - 1] == '.') name[Len - 1] = 0;
}

static void UefiNormalizeName(CHAR16 *name) {
    if (!name) return;

    UINTN len = 0;
    while (name[len] != 0) {
        if (name[len] == L';') {
            name[len] = 0;
            break;
        }
        ++len;
    }

    if (len > 0 && name[len - 1] == L'.') {
        name[len - 1] = 0;
    }
}

static void IsoGetDirectoryRecordName(ISO9660_DIR_RECORD *rec, UINT8 *record_end, char *out, UINTN out_size) {
    if (!out || out_size == 0) return;
    out[0] = 0;

    UINTN Len = rec->name_length;
    if (Len >= out_size) Len = out_size - 1;
    memcpy(out, rec->name, Len);
    out[Len] = 0;

    UINTN SysUseOffset = offsetof(ISO9660_DIR_RECORD, name) + rec->name_length;
    if (SysUseOffset & 1) SysUseOffset++;
    UINT8 *sus = (UINT8 *)rec + SysUseOffset;

    while (sus + 4 <= record_end) {
        UINT8 SusLen = sus[2];
        if (SusLen < 4 || sus + SusLen > record_end) break;

        if (sus[0] == 'N' && sus[1] == 'M' && SusLen >= 5) {
            UINT8 Flags   = sus[4];
            UINTN NameLen = SusLen - 5;
            if ((Flags & 0x06) == 0 && NameLen > 0) {
                if (NameLen >= out_size) NameLen = out_size - 1;
                memcpy(out, sus + 5, NameLen);
                out[NameLen] = 0;
            }
            break;
        }

        sus += SusLen;
    }

    IsoNormalizeName(out);
}

static int StrEndsWith_ELF(const CHAR16 *name) {
    UINTN len = 0;
    while (name[len] != 0) ++len;
    if (len < 4) return 0;
    return (name[len - 4] == L'.' &&
            (name[len - 3] == L'E' || name[len - 3] == L'e') &&
            (name[len - 2] == L'L' || name[len - 2] == L'l') &&
            (name[len - 1] == L'F' || name[len - 1] == L'f'));
}

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

#pragma pack(push, 1)
typedef struct {
    UINT16 bfType;
    UINT32 bfSize;
    UINT16 bfReserved1;
    UINT16 bfReserved2;
    UINT32 bfOffBits;
} BMP_FILE_HEADER;

typedef struct {
    UINT32 biSize;
    INT32  biWidth;
    INT32  biHeight;
    UINT16 biPlanes;
    UINT16 biBitCount;
    UINT32 biCompression;
    UINT32 biSizeImage;
    INT32  biXPelsPerMeter;
    INT32  biYPelsPerMeter;
    UINT32 biClrUsed;
    UINT32 biClrImportant;
} BMP_INFO_HEADER;
#pragma pack(pop)

#define ISO_TO_ATA(iso_lba) ((UINT64)(iso_lba) * 4ULL)

static UINT16 ReadLe16(const UINT8 *p) {
    if (!p) return 0;
    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 ReadLe32(const UINT8 *p) {
    if (!p) return 0;
    return (UINT32)p[0] |
           ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}


static void AppendString(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void TrimString(char *str) {
    if (!str || !*str) return;
    char *start = str;
    while (*start == ' ') start++;
    if (start != str) {
        char *dst = str;
        while (*start) *dst++ = *start++;
        *dst = 0;
    }
    int len = 0;
    while (str[len]) len++;
    while (len > 0 && str[len - 1] == ' ') {
        str[len - 1] = 0;
        len--;
    }
}

static BOOLEAN GuidsAreEqual(EFI_GUID *g1, EFI_GUID *g2) {
    UINT8 *b1 = (UINT8 *)g1;
    UINT8 *b2 = (UINT8 *)g2;
    for (int i = 0; i < 16; i++) {
        if (b1[i] != b2[i]) return FALSE;
    }
    return TRUE;
}

static BOOTLOADER_HANDOFF *FindBootloaderHandoff(EFI_SYSTEM_TABLE *ST) {
    EFI_GUID handoff_guid = IMPLUSOS_BOOT_HANDOFF_GUID;
    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *cfg = &ST->ConfigurationTable[i];
        if (!cfg->VendorTable) continue;
        if (!GuidsAreEqual(&cfg->VendorGuid, &handoff_guid)) continue;
        BOOTLOADER_HANDOFF *handoff = (BOOTLOADER_HANDOFF *)cfg->VendorTable;
        if (handoff->Signature == IMPLUSOS_BOOT_HANDOFF_SIGNATURE &&
            handoff->Version   == IMPLUSOS_BOOT_HANDOFF_VERSION) {
            return handoff;
        }
    }
    return NULL;
}

static char *GetSmbiosString(UEFI_SMBIOS_HEADER *hdr, UINT8 index, UINT8 *end) {
    if (index == 0) return NULL;
    char *str = (char *)hdr + hdr->Length;
    for (int i = 1; i < index; i++) {
        while ((UINT8 *)str < end && *str != 0) str++;
        str++;
        if ((UINT8 *)str >= end || *str == 0) return NULL;
    }
    if ((UINT8 *)str >= end) return NULL;
    return str;
}

static void DiscoverSMBIOS(EFI_SYSTEM_TABLE *ST, char *CPUName, char *Manufacturer, char *ProductName) {
    EFI_GUID smbios_guid  = SMBIOS_TABLE_GUID;
    EFI_GUID smbios3_guid = SMBIOS3_TABLE_GUID;
    void    *smbios       = NULL;
    BOOLEAN  is_smbios3   = FALSE;

    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        if (GuidsAreEqual(&ST->ConfigurationTable[i].VendorGuid, &smbios3_guid)) {
            smbios     = ST->ConfigurationTable[i].VendorTable;
            is_smbios3 = TRUE;
            break;
        }
        if (GuidsAreEqual(&ST->ConfigurationTable[i].VendorGuid, &smbios_guid)) {
            smbios = ST->ConfigurationTable[i].VendorTable;
        }
    }

    if (!smbios) return;

    UINT8  *table_addr  = NULL;
    UINT32  table_len   = 0;
    UINT32  num_structs = 0;

    if (is_smbios3) {
        SMBIOS3_EPS *eps3 = (SMBIOS3_EPS *)smbios;
        if (eps3->AnchorString[0] != '_' || eps3->AnchorString[1] != 'S' ||
            eps3->AnchorString[2] != 'M' || eps3->AnchorString[3] != '3' ||
            eps3->AnchorString[4] != '_') return;
        table_addr  = (UINT8 *)(UINTN)eps3->StructureTableAddress;
        table_len   = eps3->StructureTableMaximumSize;
        num_structs = 0xFFFFFFFF;
    } else {
        SMBIOS_EPS *eps = (SMBIOS_EPS *)smbios;
        if (eps->AnchorString[0] != '_' || eps->AnchorString[1] != 'S' ||
            eps->AnchorString[2] != 'M' || eps->AnchorString[3] != '_') return;
        table_addr  = (UINT8 *)(UINTN)eps->StructureTableAddress;
        table_len   = eps->StructureTableLength;
        num_structs = eps->NumberOfSMBIOSStructures;
    }

    if (!table_addr || table_len == 0) return;

    UINT8  *ptr          = table_addr;
    UINT8  *end          = table_addr + table_len;
    UINT32  struct_count = 0;

    while (ptr + sizeof(UEFI_SMBIOS_HEADER) <= end && struct_count < num_structs) {
        UEFI_SMBIOS_HEADER *hdr = (UEFI_SMBIOS_HEADER *)ptr;
        if (hdr->Type == 127) break;
        if (hdr->Length < sizeof(UEFI_SMBIOS_HEADER)) break;

        if (hdr->Type == 1 && hdr->Length >= 6) {
            UINT8  mfg_idx  = ptr[0x04];
            UINT8  prod_idx = ptr[0x05];
            char  *mfg  = GetSmbiosString(hdr, mfg_idx,  end);
            char  *prod = GetSmbiosString(hdr, prod_idx, end);
            if (mfg && Manufacturer[0] == 0) {
                UINTN len = 0;
                while (mfg[len] && len < 63 && (UINT8 *)&mfg[len] < end) len++;
                memcpy(Manufacturer, mfg, len);
                Manufacturer[len] = 0;
                TrimString(Manufacturer);
            }
            if (prod && ProductName[0] == 0) {
                UINTN len = 0;
                while (prod[len] && len < 63 && (UINT8 *)&prod[len] < end) len++;
                memcpy(ProductName, prod, len);
                ProductName[len] = 0;
                TrimString(ProductName);
            }
        } else if (hdr->Type == 4 && hdr->Length >= 0x11) {
            UINT8  proc_idx = ptr[0x10];
            char  *proc     = GetSmbiosString(hdr, proc_idx, end);
            if (proc && CPUName[0] == 0) {
                UINTN len = 0;
                while (proc[len] && len < 63 && (UINT8 *)&proc[len] < end) len++;
                memcpy(CPUName, proc, len);
                CPUName[len] = 0;
                TrimString(CPUName);
            }
        }

        ptr += hdr->Length;
        while (ptr + 1 < end) {
            if (ptr[0] == 0 && ptr[1] == 0) { ptr += 2; break; }
            ptr++;
        }
        if (ptr >= end) break;
        struct_count++;
    }
}

static UINT64 ParseElToritoCatalog(EFI_BLOCK_IO_PROTOCOL *Bio, EFI_SYSTEM_TABLE *ST) {
    UINT32 BS = Bio->Media->BlockSize;
    if (BS < 512 || BS > 4096) return 0;

    UINT8      *Buf;
    EFI_STATUS  Status = uefi_call_wrapper(
        ST->BootServices->AllocatePool, 3, EfiLoaderData, BS, (VOID **)&Buf);
    if (EFI_ERROR(Status)) return 0;

    Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, 0, BS, Buf);
    if (!EFI_ERROR(Status) && Buf[510] == 0x55 && Buf[511] == 0xAA) {
        uint8_t *pt = Buf + 0x1BE;
        for (int p = 0; p < 4; p++) {
            uint8_t  type = pt[p * 16 + 4];
            uint32_t lba  = *(uint32_t *)(pt + p * 16 + 8);
            if ((type == 0xEF || type == 0x0C || type == 0x0B) && lba != 0) {
                uefi_call_wrapper(ST->BootServices->FreePool, 1, Buf);
                return lba;
            }
        }
    }

    UINT64 Result = 0;
    UINT64 Lba16  = (16ULL * 2048ULL) / (UINT64)BS;

    Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba16, BS, Buf);
    if (EFI_ERROR(Status) ||
        Buf[1] != 'C' || Buf[2] != 'D' ||
        Buf[3] != '0' || Buf[4] != '0' || Buf[5] != '1') {
        goto done;
    }

    for (UINT32 S = 17; S < 32 && !Result; S++) {
        UINT64 Lba = ((UINT64)S * 2048ULL) / (UINT64)BS;
        Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba, BS, Buf);
        if (EFI_ERROR(Status)) break;
        if (Buf[0] == 0xFF) break;
        if (Buf[0] != 0x00) continue;

        UINT32 CatIsoLBA = (UINT32)Buf[71] | ((UINT32)Buf[72] << 8)
                         | ((UINT32)Buf[73] << 16) | ((UINT32)Buf[74] << 24);
        if (CatIsoLBA == 0) break;

        UINT64 CatBlock = ((UINT64)CatIsoLBA * 2048ULL) / (UINT64)BS;
        Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, CatBlock, BS, Buf);
        if (EFI_ERROR(Status)) break;

        {
            UINT8 *E = Buf + 32;
            if (E[0] == 0x88) {
                UINT32 RBA = (UINT32)E[8]  | ((UINT32)E[9]  << 8)
                           | ((UINT32)E[10] << 16) | ((UINT32)E[11] << 24);
                if (RBA) Result = ISO_TO_ATA(RBA);
            }
        }

        if (!Result) {
            UINT32 Off = 64;
            while (Off + 64 <= BS) {
                UINT8  Hdr        = Buf[Off];
                if (Hdr != 0x90 && Hdr != 0x91) break;
                UINT16 EntryCount = (UINT16)Buf[Off + 2] | ((UINT16)Buf[Off + 3] << 8);
                Off += 32;
                for (UINT16 E = 0; E < EntryCount && Off + 32 <= BS; E++, Off += 32) {
                    if (Buf[Off] != 0x88) continue;
                    UINT32 RBA = (UINT32)Buf[Off + 8]  | ((UINT32)Buf[Off + 9]  << 8)
                               | ((UINT32)Buf[Off + 10] << 16) | ((UINT32)Buf[Off + 11] << 24);
                    if (RBA) { Result = ISO_TO_ATA(RBA); goto done; }
                }
                if (Hdr == 0x91) break;
            }
        }
    }

done:
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Buf);
    return Result;
}

static UINT64 GetPartitionStartLBA(EFI_HANDLE DeviceHandle, EFI_SYSTEM_TABLE *ST, BOOT_INFO *BootInfo) {
    BootInfo->BootDriveType = BOOT_DRIVE_TYPE_UNKNOWN;

    EFI_DEVICE_PATH_PROTOCOL *DevicePath = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        DeviceHandle, &gEfiDevicePathProtocolGuid, (VOID **)&DevicePath);

    if (!EFI_ERROR(Status) && DevicePath != NULL) {
        EFI_DEVICE_PATH_PROTOCOL *Node = DevicePath;
        while (!IsDevicePathEnd(Node)) {
            if (DevicePathType(Node) == 3) {
                if (DevicePathSubType(Node) == 5)
                    BootInfo->BootDriveType = BOOT_DRIVE_TYPE_USB;
                else if (DevicePathSubType(Node) == 1 || DevicePathSubType(Node) == 18)
                    BootInfo->BootDriveType = BOOT_DRIVE_TYPE_IDE;
            }
            if (DevicePathType(Node) == 4 && DevicePathSubType(Node) == 1) {
                HARDDRIVE_DEVICE_PATH *HD = (HARDDRIVE_DEVICE_PATH *)Node;
                return HD->PartitionStart;
            }
            Node = NextDevicePathNode(Node);
        }
    }

    UINTN       Count   = 0;
    EFI_HANDLE *Handles = NULL;
    Status = uefi_call_wrapper(
        ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);

    if (!EFI_ERROR(Status) && Handles != NULL) {
        for (UINTN i = 0; i < Count; i++) {
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

        for (UINTN i = 0; i < Count; i++) {
            if (Handles[i] == DeviceHandle) continue;
            EFI_DEVICE_PATH_PROTOCOL *DP = NULL;
            Status = uefi_call_wrapper(
                ST->BootServices->HandleProtocol, 3,
                Handles[i], &gEfiDevicePathProtocolGuid, (VOID **)&DP);
            if (EFI_ERROR(Status) || !DP) continue;
            EFI_DEVICE_PATH_PROTOCOL *Node = DP;
            while (!IsDevicePathEnd(Node)) {
                if (DevicePathType(Node) == 3) {
                    if (DevicePathSubType(Node) == 5)
                        BootInfo->BootDriveType = BOOT_DRIVE_TYPE_USB;
                    else if (DevicePathSubType(Node) == 1 || DevicePathSubType(Node) == 18)
                        BootInfo->BootDriveType = BOOT_DRIVE_TYPE_IDE;
                }
                if (DevicePathType(Node) == 4 && DevicePathSubType(Node) == 1) {
                    HARDDRIVE_DEVICE_PATH *HD = (HARDDRIVE_DEVICE_PATH *)Node;
                    uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
                    return HD->PartitionStart;
                }
                Node = NextDevicePathNode(Node);
            }
        }

        uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
    }

    return 0ULL;
}

static void DiscoverAcpiRsdp(EFI_SYSTEM_TABLE *ST, BOOT_INFO *BootInfo) {
    if (!ST || !BootInfo) return;

    EFI_GUID acpi10 = ACPI_TABLE_GUID;
    EFI_GUID acpi20 = ACPI_20_TABLE_GUID;

    BootInfo->AcpiRsdpAddress  = 0;
    BootInfo->AcpiRsdpSize     = 0;
    BootInfo->AcpiRsdpRevision = 0;

    for (int pass = 0; pass < 2; ++pass) {
        EFI_GUID *target = (pass == 0) ? &acpi20 : &acpi10;
        for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
            EFI_CONFIGURATION_TABLE *cfg = &ST->ConfigurationTable[i];
            if (!cfg || !cfg->VendorTable) continue;
            if (!GuidsAreEqual(&cfg->VendorGuid, target)) continue;
            const CHAR8 *sig = (const CHAR8 *)cfg->VendorTable;
            if (sig[0] != 'R' || sig[1] != 'S' || sig[2] != 'D' || sig[3] != ' ' ||
                sig[4] != 'P' || sig[5] != 'T' || sig[6] != 'R' || sig[7] != ' ') continue;
            BootInfo->AcpiRsdpAddress  = (uint64_t)(UINTN)cfg->VendorTable;
            ACPI_RSDP_V1 *v1           = (ACPI_RSDP_V1 *)cfg->VendorTable;
            BootInfo->AcpiRsdpRevision = v1->Revision;
            BootInfo->AcpiRsdpSize     = (UINT32)sizeof(ACPI_RSDP_V1);
            if (v1->Revision >= 2) {
                ACPI_RSDP_V2 *v2 = (ACPI_RSDP_V2 *)cfg->VendorTable;
                if (v2->Length > 0) BootInfo->AcpiRsdpSize = v2->Length;
            }
            return;
        }
    }
}

static EFI_FILE_PROTOCOL *OpenFsRootFromHandle(EFI_HANDLE Handle, EFI_SYSTEM_TABLE *ST) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs   = NULL;
    EFI_FILE_PROTOCOL               *Root = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR(Status) || !Fs) return NULL;
    Status = uefi_call_wrapper(Fs->OpenVolume, 2, Fs, &Root);
    if (EFI_ERROR(Status)) return NULL;
    return Root;
}

static EFI_STATUS FindISOPath(EFI_SYSTEM_TABLE *ST, const char *IsoPath,
                               UINT32 *LbaOut, UINT32 *SizeOut, BOOLEAN *IsDirOut) {
    UINTN       Count   = 0;
    EFI_HANDLE *Handles = NULL;
    EFI_STATUS  Status  = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || !Handles) return Status;

    EFI_STATUS Result = EFI_NOT_FOUND;
    for (UINTN i = 0; i < Count; i++) {
        EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
        uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
            Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
        if (!Bio || !Bio->Media) continue;

        UINT32 BS = Bio->Media->BlockSize;
        if (BS < 512 || BS > 4096) continue;

        UINT8 *SectorBuf = NULL;
        Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, 2048, (VOID **)&SectorBuf);
        if (EFI_ERROR(Status)) continue;

        UINT64 Lba16 = (16ULL * 2048ULL) / (UINT64)BS;
        Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba16, 2048, SectorBuf);
        if (!EFI_ERROR(Status) &&
            SectorBuf[1] == 'C' && SectorBuf[2] == 'D' &&
            SectorBuf[3] == '0' && SectorBuf[4] == '0' && SectorBuf[5] == '1') {
            ISO9660_PVD *Pvd    = (ISO9660_PVD *)SectorBuf;
            UINT32       CurLba = Pvd->root_dir_record.extent_lba_le;
            UINT32       CurSize = Pvd->root_dir_record.data_length_le;
            BOOLEAN      CurIsDir = TRUE;

            const char *p = IsoPath;
            while (*p == '/') p++;
            BOOLEAN FoundPath = TRUE;

            while (*p) {
                char Component[128];
                int n = 0;
                while (p[n] && p[n] != '/' && n < 127) { Component[n] = p[n]; n++; }
                Component[n] = 0;

                UINT32  Sectors   = (CurSize + 2047) / 2048;
                BOOLEAN FoundComp = FALSE;
                for (UINT32 s = 0; s < Sectors; s++) {
                    Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                        ((UINT64)(CurLba + s) * 2048ULL) / (UINT64)BS, 2048, SectorBuf);
                    if (EFI_ERROR(Status)) break;

                    UINT8 *ptr = SectorBuf;
                    while (ptr < SectorBuf + 2048 && *ptr != 0) {
                        ISO9660_DIR_RECORD *rec = (ISO9660_DIR_RECORD *)ptr;
                        if (rec->length == 0 || ptr + rec->length > SectorBuf + 2048) break;
                        if (rec->name_length > 0 && rec->name[0] != 0 && rec->name[0] != 1) {
                            char EntryName[256];
                            IsoGetDirectoryRecordName(rec, ptr + rec->length, EntryName, sizeof(EntryName));
                            if (strcasecmp((const char *)EntryName, Component) == 0) {
                                CurLba   = rec->extent_lba_le;
                                CurSize  = rec->data_length_le;
                                CurIsDir = (rec->flags & 2) != 0;
                                FoundComp = TRUE;
                                break;
                            }
                        }
                        ptr += rec->length;
                    }
                    if (FoundComp) break;
                }

                if (!FoundComp) { FoundPath = FALSE; break; }
                p += n;
                while (*p == '/') p++;
            }

            if (FoundPath) {
                if (LbaOut)   *LbaOut   = CurLba;
                if (SizeOut)  *SizeOut  = CurSize;
                if (IsDirOut) *IsDirOut = CurIsDir;
                Result = EFI_SUCCESS;
            }
        }

        uefi_call_wrapper(ST->BootServices->FreePool, 1, SectorBuf);
        if (!EFI_ERROR(Result)) break;
    }

    uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
    return Result;
}

static EFI_STATUS LoadFromISO(EFI_SYSTEM_TABLE *ST, const char *IsoPath, VOID **Buffer, UINTN *Size) {
    UINTN       Count   = 0;
    EFI_HANDLE *Handles = NULL;
    EFI_STATUS  Status  = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || !Handles) return Status;

    EFI_STATUS Result = EFI_NOT_FOUND;
    for (UINTN i = 0; i < Count; i++) {
        EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
        uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
            Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
        if (!Bio || !Bio->Media) continue;

        UINT32 BS = Bio->Media->BlockSize;
        if (BS < 512 || BS > 4096) continue;

        UINT8 *SectorBuf = NULL;
        Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, 2048, (VOID **)&SectorBuf);
        if (EFI_ERROR(Status)) continue;

        UINT64 Lba16 = (16ULL * 2048ULL) / (UINT64)BS;
        Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba16, 2048, SectorBuf);
        if (!EFI_ERROR(Status) &&
            SectorBuf[1] == 'C' && SectorBuf[2] == 'D' &&
            SectorBuf[3] == '0' && SectorBuf[4] == '0' && SectorBuf[5] == '1') {
            ISO9660_PVD *Pvd    = (ISO9660_PVD *)SectorBuf;
            UINT32       CurLba = Pvd->root_dir_record.extent_lba_le;
            UINT32       CurSize = Pvd->root_dir_record.data_length_le;
            BOOLEAN      CurIsDir = TRUE;

            const char *p = IsoPath;
            while (*p == '/') p++;
            BOOLEAN FoundPath = TRUE;

            while (*p) {
                char Component[128];
                int n = 0;
                while (p[n] && p[n] != '/' && n < 127) { Component[n] = p[n]; n++; }
                Component[n] = 0;

                UINT32  Sectors   = (CurSize + 2047) / 2048;
                BOOLEAN FoundComp = FALSE;
                for (UINT32 s = 0; s < Sectors; s++) {
                    uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                        ((UINT64)(CurLba + s) * 2048ULL) / (UINT64)BS, 2048, SectorBuf);

                    UINT8 *ptr = SectorBuf;
                    while (ptr < SectorBuf + 2048 && *ptr != 0) {
                        ISO9660_DIR_RECORD *rec = (ISO9660_DIR_RECORD *)ptr;
                        if (rec->length == 0 || ptr + rec->length > SectorBuf + 2048) break;
                        if (rec->name_length > 0 && rec->name[0] != 0 && rec->name[0] != 1) {
                            char EntryName[256];
                            IsoGetDirectoryRecordName(rec, ptr + rec->length, EntryName, sizeof(EntryName));
                            if (strcasecmp((const char *)EntryName, Component) == 0) {
                                CurLba    = rec->extent_lba_le;
                                CurSize   = rec->data_length_le;
                                CurIsDir  = (rec->flags & 2) != 0;
                                FoundComp = TRUE;
                                break;
                            }
                        }
                        ptr += rec->length;
                    }
                    if (FoundComp) break;
                }
                if (!FoundComp) { FoundPath = FALSE; break; }
                p += n;
                while (*p == '/') p++;
            }

            if (FoundPath && !CurIsDir) {
                EFI_PHYSICAL_ADDRESS FileAddr = 0xFFFFFFFFULL;
                Status = uefi_call_wrapper(ST->BootServices->AllocatePages, 4,
                    AllocateMaxAddress, EfiLoaderData, EFI_SIZE_TO_PAGES(CurSize), &FileAddr);
                if (!EFI_ERROR(Status)) {
                    VOID *FileBuf = (VOID *)(UINTN)FileAddr;
                    UINT32 Rem = CurSize;
                    UINT32 L   = CurLba;
                    UINT8 *D   = (UINT8 *)FileBuf;

                    UINT8 *TempBuf = NULL;
                    uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, 2048, (VOID **)&TempBuf);

                    while (Rem > 0 && TempBuf) {
                        UINT64 StartByte   = (UINT64)L * 2048ULL;
                        UINT64 StartSector = StartByte / (UINT64)BS;
                        UINT32 ReadBytes   = ((2048 + BS - 1) / BS) * BS;

                        Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                            StartSector, ReadBytes, TempBuf);
                        if (EFI_ERROR(Status)) break;

                        UINT32 Copy = (Rem < 2048) ? Rem : 2048;
                        memcpy(D, TempBuf, Copy);
                        D += Copy; Rem -= Copy; L++;
                    }
                    if (TempBuf)
                        uefi_call_wrapper(ST->BootServices->FreePool, 1, TempBuf);

                    if (Rem == 0) {
                        *Buffer = FileBuf;
                        *Size   = CurSize;
                        Result  = EFI_SUCCESS;
                    } else {
                        uefi_call_wrapper(ST->BootServices->FreePages, 2, FileAddr, EFI_SIZE_TO_PAGES(CurSize));
                        Result = EFI_BUFFER_TOO_SMALL;
                    }
                }
            }
        }
        uefi_call_wrapper(ST->BootServices->FreePool, 1, SectorBuf);
        if (!EFI_ERROR(Result)) break;
    }
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
    return Result;
}

static EFI_STATUS PreloadDriversFromISO(EFI_SYSTEM_TABLE *ST, BOOT_INFO *BootInfo) {
    UINTN       Count   = 0;
    EFI_HANDLE *Handles = NULL;
    EFI_STATUS  Status  = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || !Handles) return Status;

    for (UINTN i = 0; i < Count; i++) {
        EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
        uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
            Handles[i], &gEfiBlockIoProtocolGuid, (VOID **)&Bio);
        if (!Bio || !Bio->Media) continue;

        UINT32 BS = Bio->Media->BlockSize;
        if (BS < 512 || BS > 4096) continue;

        UINT8 *SectorBuf = NULL;
        Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3, EfiLoaderData, 2048, (VOID **)&SectorBuf);
        if (EFI_ERROR(Status)) continue;

        UINT64 Lba16 = (16ULL * 2048ULL) / (UINT64)BS;
        Status = uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId, Lba16, 2048, SectorBuf);
        if (!EFI_ERROR(Status) &&
            SectorBuf[1] == 'C' && SectorBuf[2] == 'D' &&
            SectorBuf[3] == '0' && SectorBuf[4] == '0' && SectorBuf[5] == '1') {
            ISO9660_PVD *Pvd    = (ISO9660_PVD *)SectorBuf;
            UINT32       CurLba = Pvd->root_dir_record.extent_lba_le;
            UINT32       CurSize = Pvd->root_dir_record.data_length_le;

            const char *DirPath[] = { "KERNEL", "DRIVER" };
            BOOLEAN FoundDir = TRUE;
            for (int j = 0; j < 2; j++) {
                UINT32  Sectors   = (CurSize + 2047) / 2048;
                BOOLEAN FoundComp = FALSE;
                for (UINT32 s = 0; s < Sectors; s++) {
                    uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                        ((UINT64)(CurLba + s) * 2048ULL) / (UINT64)BS, 2048, SectorBuf);
                    UINT8 *ptr = SectorBuf;
                    while (ptr < SectorBuf + 2048 && *ptr != 0) {
                        ISO9660_DIR_RECORD *rec = (ISO9660_DIR_RECORD *)ptr;
                        if (rec->length == 0 || ptr + rec->length > SectorBuf + 2048) break;
                        if (rec->name_length > 0 && rec->name[0] != 0 && rec->name[0] != 1) {
                            char EntryName[256];
                            IsoGetDirectoryRecordName(rec, ptr + rec->length, EntryName, sizeof(EntryName));
                            if (strcasecmp((const char *)EntryName, DirPath[j]) == 0) {
                                CurLba    = rec->extent_lba_le;
                                CurSize   = rec->data_length_le;
                                FoundComp = TRUE;
                                break;
                            }
                        }
                        ptr += rec->length;
                    }
                    if (FoundComp) break;
                }
                if (!FoundComp) { FoundDir = FALSE; break; }
            }

            if (FoundDir) {
                UINT32 Sectors = (CurSize + 2047) / 2048;
                for (UINT32 s = 0; s < Sectors; s++) {
                    uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                        ((UINT64)(CurLba + s) * 2048ULL) / (UINT64)BS, 2048, SectorBuf);
                    UINT8 *ptr = SectorBuf;
                    while (ptr < SectorBuf + 2048 && *ptr != 0) {
                        ISO9660_DIR_RECORD *rec = (ISO9660_DIR_RECORD *)ptr;
                        if (rec->length == 0 || ptr + rec->length > SectorBuf + 2048) break;
                        if (rec->name_length > 0 && rec->name[0] != 0 &&
                            rec->name[0] != 1 && !(rec->flags & 2)) {
                            char EntryName[256];
                            IsoGetDirectoryRecordName(rec, ptr + rec->length, EntryName, sizeof(EntryName));
                            if (StrEndsWith_ELF_8(EntryName)) {
                                if (BootInfo->LoadedFileCount < MAX_LOADED_FILES) {
                                    VOID *FileBuf = NULL;
                                    uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
                                        EfiLoaderData, rec->data_length_le, &FileBuf);
                                    if (FileBuf) {
                                        UINT32 Rem = rec->data_length_le;
                                        UINT32 L   = rec->extent_lba_le;
                                        UINT8 *D   = (UINT8 *)FileBuf;
                                        UINT8  TempSector[2048];
                                        while (Rem > 0) {
                                            uefi_call_wrapper(Bio->ReadBlocks, 5, Bio, Bio->Media->MediaId,
                                                ((UINT64)L * 2048ULL) / (UINT64)BS, 2048, TempSector);
                                            UINT32 Copy = (Rem < 2048) ? Rem : 2048;
                                            memcpy(D, TempSector, Copy);
                                            D += Copy; Rem -= Copy; L++;
                                        }
                                        UINTN idx = BootInfo->LoadedFileCount++;
                                        UINTN len = 0;
                                        while (EntryName[len] && len < LOADED_FILE_NAME_MAX - 1) {
                                            BootInfo->LoadedFiles[idx].Name[len] = EntryName[len];
                                            len++;
                                        }
                                        BootInfo->LoadedFiles[idx].Name[len]    = '\0';
                                        BootInfo->LoadedFiles[idx].PhysAddr     = (EFI_PHYSICAL_ADDRESS)(UINTN)FileBuf;
                                        BootInfo->LoadedFiles[idx].Size         = rec->data_length_le;
                                    }
                                }
                            }
                        }
                        ptr += rec->length;
                    }
                }
            }
        }
        uefi_call_wrapper(ST->BootServices->FreePool, 1, SectorBuf);
    }
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
    return EFI_SUCCESS;
}

EFI_STATUS LoadFileToMemory(
    EFI_SYSTEM_TABLE  *ST,
    EFI_FILE_PROTOCOL *File,
    VOID             **Buffer,
    UINTN             *Size
) {
    EFI_FILE_INFO *Info;
    UINTN          InfoSize = sizeof(EFI_FILE_INFO) + 256;

    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->AllocatePool, 3, EfiLoaderData, InfoSize, (VOID **)&Info);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(File->GetInfo, 4, File, &gEfiFileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(ST->BootServices->FreePool, 1, Info);
        return Status;
    }

    *Size = Info->FileSize;
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Info);

    Status = uefi_call_wrapper(
        ST->BootServices->AllocatePool, 3, EfiLoaderData, *Size, Buffer);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(File->Read, 3, File, Size, *Buffer);
    return Status;
}

static EFI_STATUS LoadFileToMemoryBelow4G(
    EFI_SYSTEM_TABLE  *ST,
    EFI_FILE_PROTOCOL *File,
    VOID             **Buffer,
    UINTN             *Size
) {
    EFI_FILE_INFO *Info;
    UINTN          InfoSize = sizeof(EFI_FILE_INFO) + 256;

    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->AllocatePool, 3, EfiLoaderData, InfoSize, (VOID **)&Info);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(File->GetInfo, 4, File, &gEfiFileInfoGuid, &InfoSize, Info);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(ST->BootServices->FreePool, 1, Info);
        return Status;
    }

    UINTN FileSize = Info->FileSize;
    uefi_call_wrapper(ST->BootServices->FreePool, 1, Info);

    if (FileSize == 0) { *Buffer = NULL; *Size = 0; return EFI_SUCCESS; }

    UINTN                Pages   = EFI_SIZE_TO_PAGES(FileSize);
    EFI_PHYSICAL_ADDRESS MaxAddr = 0xFFFFFFFFULL;
    Status = uefi_call_wrapper(
        ST->BootServices->AllocatePages, 4,
        AllocateMaxAddress, EfiLoaderData, Pages, &MaxAddr);
    if (EFI_ERROR(Status)) return Status;

    *Buffer = (VOID *)(UINTN)MaxAddr;
    *Size   = FileSize;
    UINTN ReadSize = FileSize;
    Status = uefi_call_wrapper(File->Read, 3, File, &ReadSize, *Buffer);
    if (EFI_ERROR(Status) || ReadSize != FileSize) {
        uefi_call_wrapper(ST->BootServices->FreePages, 2, MaxAddr, Pages);
        return EFI_LOAD_ERROR;
    }
    return EFI_SUCCESS;
}

EFI_STATUS LoadKernelELF(
    EFI_SYSTEM_TABLE *ST,
    VOID             *KernelImage,
    UINTN             KernelImageSize,
    UINT64           *EntryPoint
) {
    Elf64_Ehdr *Ehdr = (Elf64_Ehdr *)KernelImage;

    if (Ehdr->e_ident[0] != 0x7F || Ehdr->e_ident[1] != 'E' ||
        Ehdr->e_ident[2] != 'L'  || Ehdr->e_ident[3] != 'F') {
        return EFI_LOAD_ERROR;
    }

    if (Ehdr->e_phoff >= KernelImageSize ||
        Ehdr->e_phoff + (Ehdr->e_phnum * sizeof(Elf64_Phdr)) > KernelImageSize) {
        return EFI_LOAD_ERROR;
    }

    Elf64_Phdr *Phdrs = (Elf64_Phdr *)((UINT8 *)KernelImage + Ehdr->e_phoff);

    UINT64 MinAddr     = 0xFFFFFFFFFFFFFFFF;
    UINT64 MaxAddr     = 0;
    UINTN  LoadedCount = 0;

    for (UINTN i = 0; i < Ehdr->e_phnum; i++) {
        if (Phdrs[i].p_type != PT_LOAD) continue;
        if (Phdrs[i].p_vaddr < MinAddr) MinAddr = Phdrs[i].p_vaddr;
        if (Phdrs[i].p_vaddr + Phdrs[i].p_memsz > MaxAddr) MaxAddr = Phdrs[i].p_vaddr + Phdrs[i].p_memsz;
        LoadedCount++;
    }

    if (LoadedCount == 0 || MaxAddr <= MinAddr) return EFI_LOAD_ERROR;

    UINTN  TotalPages  = EFI_SIZE_TO_PAGES(MaxAddr - MinAddr);
    UINT64 KernelSpan  = (UINT64)TotalPages * EFI_PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS KernelBaseAddr = MinAddr;
    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->AllocatePages, 4,
        AllocateAddress, EfiLoaderData, TotalPages, &KernelBaseAddr);

    if (EFI_ERROR(Status) && Ehdr->e_type == ET_DYN) {
        KernelBaseAddr = 0;
        for (EFI_PHYSICAL_ADDRESS Candidate = 0x01000000ULL;
             Candidate + KernelSpan <= 0x80000000ULL;
             Candidate += 0x00200000ULL) {
            EFI_PHYSICAL_ADDRESS Requested = Candidate;
            Status = uefi_call_wrapper(
                ST->BootServices->AllocatePages, 4,
                AllocateAddress, EfiLoaderData, TotalPages, &Requested);
            if (!EFI_ERROR(Status)) {
                KernelBaseAddr = Requested;
                break;
            }
        }
    }

    if (EFI_ERROR(Status) || KernelBaseAddr == 0) {
        return EFI_ERROR(Status) ? Status : EFI_LOAD_ERROR;
    }

    for (UINTN i = 0; i < Ehdr->e_phnum; i++) {
        Elf64_Phdr *Ph = &Phdrs[i];
        if (Ph->p_type != PT_LOAD) continue;

        if (Ph->p_offset >= KernelImageSize ||
            Ph->p_offset + Ph->p_filesz > KernelImageSize ||
            Ph->p_memsz < Ph->p_filesz) {
            uefi_call_wrapper(ST->BootServices->FreePages, 2, KernelBaseAddr, TotalPages);
            return EFI_LOAD_ERROR;
        }

        EFI_PHYSICAL_ADDRESS Dest = KernelBaseAddr + (Ph->p_vaddr - MinAddr);
        memcpy((VOID *)(UINTN)Dest, (UINT8 *)KernelImage + Ph->p_offset, Ph->p_filesz);
        memset((VOID *)(UINTN)(Dest + Ph->p_filesz), 0, Ph->p_memsz - Ph->p_filesz);
    }

    UINT64 LoadBias = KernelBaseAddr - MinAddr;

    if (Ehdr->e_shoff != 0 && Ehdr->e_shnum != 0) {
        Elf64_Shdr *Shdrs = (Elf64_Shdr *)((UINT8 *)KernelImage + Ehdr->e_shoff);
        for (UINTN i = 0; i < Ehdr->e_shnum; i++) {
            if (Shdrs[i].sh_type == SHT_RELA) {
                Elf64_Rela *Relas     = (Elf64_Rela *)((UINT8 *)KernelImage + Shdrs[i].sh_offset);
                UINTN       RelasCount = Shdrs[i].sh_size / sizeof(Elf64_Rela);
                for (UINTN j = 0; j < RelasCount; j++) {
                    UINT32 type = (UINT32)(Relas[j].r_info & 0xFFFFFFFF);
                    if (type == R_X86_64_RELATIVE) {
                        UINT64 *Target = (UINT64 *)(UINTN)(KernelBaseAddr + (Relas[j].r_offset - MinAddr));
                        *Target = LoadBias + Relas[j].r_addend;
                    }
                }
            }
        }
    }

    *EntryPoint = KernelBaseAddr + (Ehdr->e_entry - MinAddr);
    return EFI_SUCCESS;
}

static void Char16ToChar8(const CHAR16 *src, char *dst, UINTN dst_size) {
    UINTN i = 0;
    while (src[i] != 0 && i + 1 < dst_size) { dst[i] = (char)(src[i] & 0x7F); ++i; }
    dst[i] = '\0';
}

static EFI_STATUS PreloadDriverModules(
    EFI_SYSTEM_TABLE  *ST,
    EFI_FILE_PROTOCOL *Root,
    BOOT_INFO         *BootInfo
) {
    BootInfo->LoadedFileCount = 0;

    EFI_FILE_PROTOCOL *DriverDir = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        Root->Open, 5, Root, &DriverDir, L"Kernel\\Driver", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) return EFI_SUCCESS;

    UINTN  InfoBufSize = sizeof(EFI_FILE_INFO) + 512;
    VOID  *InfoBuf     = NULL;
    Status = uefi_call_wrapper(
        ST->BootServices->AllocatePool, 3, EfiLoaderData, InfoBufSize, &InfoBuf);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(DriverDir->Close, 1, DriverDir);
        return Status;
    }

    while (BootInfo->LoadedFileCount < MAX_LOADED_FILES) {
        UINTN ReadSize = InfoBufSize;
        Status = uefi_call_wrapper(DriverDir->Read, 3, DriverDir, &ReadSize, InfoBuf);
        if (EFI_ERROR(Status) || ReadSize == 0) break;

        EFI_FILE_INFO *FileInfo = (EFI_FILE_INFO *)InfoBuf;
        if (FileInfo->Attribute & EFI_FILE_DIRECTORY) continue;

        UefiNormalizeName(FileInfo->FileName);
        if (!StrEndsWith_ELF(FileInfo->FileName)) continue;

        CHAR16        FullPath[256];
        UINTN         pos    = 0;
        const CHAR16 *prefix = L"KERNEL\\DRIVER\\";
        for (UINTN p = 0; prefix[p] != 0 && pos < 240; ++p) FullPath[pos++] = prefix[p];
        for (UINTN p = 0; FileInfo->FileName[p] != 0 && pos < 254; ++p) FullPath[pos++] = FileInfo->FileName[p];
        FullPath[pos] = 0;

        EFI_FILE_PROTOCOL *File = NULL;
        Status = uefi_call_wrapper(Root->Open, 5, Root, &File, FullPath, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(Status)) continue;

        VOID  *Buffer = NULL;
        UINTN  Size   = 0;
        Status = LoadFileToMemoryBelow4G(ST, File, &Buffer, &Size);
        uefi_call_wrapper(File->Close, 1, File);
        if (EFI_ERROR(Status) || Size == 0) continue;

        UINTN idx = BootInfo->LoadedFileCount++;
        Char16ToChar8(FileInfo->FileName, BootInfo->LoadedFiles[idx].Name, LOADED_FILE_NAME_MAX);
        BootInfo->LoadedFiles[idx].PhysAddr = (EFI_PHYSICAL_ADDRESS)(UINTN)Buffer;
        BootInfo->LoadedFiles[idx].Size     = (uint64_t)Size;
    }

    uefi_call_wrapper(ST->BootServices->FreePool, 1, InfoBuf);
    uefi_call_wrapper(DriverDir->Close, 1, DriverDir);
    return EFI_SUCCESS;
}

EFI_STATUS ExitBootServicesComplete(
    EFI_HANDLE        ImageHandle,
    EFI_SYSTEM_TABLE *ST,
    BOOT_INFO        *BootInfo
) {
    UINTN      MapKey;
    UINTN      BufferSize;
    EFI_STATUS Status;

    while (1) {
        BufferSize = 0;
        Status = uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5,
            &BufferSize, NULL, &MapKey,
            &BootInfo->MemoryMapDescriptorSize, &BootInfo->MemoryMapDescriptorVersion);
        if (Status != EFI_BUFFER_TOO_SMALL) return Status;

        BufferSize += BootInfo->MemoryMapDescriptorSize * 8;
        Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
            EfiLoaderData, BufferSize, (VOID **)&BootInfo->MemoryMap);
        if (EFI_ERROR(Status)) return Status;

        Status = uefi_call_wrapper(ST->BootServices->GetMemoryMap, 5,
            &BufferSize, (EFI_MEMORY_DESCRIPTOR *)(UINTN)BootInfo->MemoryMap, &MapKey,
            (UINTN *)&BootInfo->MemoryMapDescriptorSize, &BootInfo->MemoryMapDescriptorVersion);
        if (EFI_ERROR(Status)) {
            uefi_call_wrapper(ST->BootServices->FreePool, 1, BootInfo->MemoryMap);
            continue;
        }

        BootInfo->MemoryMapSize = BufferSize;

        Status = uefi_call_wrapper(ST->BootServices->ExitBootServices, 2, ImageHandle, MapKey);
        if (Status == EFI_SUCCESS) break;

        uefi_call_wrapper(ST->BootServices->FreePool, 1, BootInfo->MemoryMap);
    }

    return EFI_SUCCESS;
}

static inline UINT32 AlphaBlend(UINT32 dst, UINT8 r, UINT8 g, UINT8 b, UINT8 a) {
    UINT8 dr = (dst >> 16) & 0xFF;
    UINT8 dg = (dst >> 8)  & 0xFF;
    UINT8 db = (dst >> 0)  & 0xFF;
    UINT8 nr = (UINT8)((r * a + dr * (255 - a)) / 255);
    UINT8 ng = (UINT8)((g * a + dg * (255 - a)) / 255);
    UINT8 nb = (UINT8)((b * a + db * (255 - a)) / 255);
    return (0xFF << 24) | ((UINT32)nr << 16) | ((UINT32)ng << 8) | nb;
}

static BOOLEAN IsFrameBufferDirect(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop) {
    EFI_GRAPHICS_PIXEL_FORMAT fmt = Gop->Mode->Info->PixelFormat;
    return (fmt == PixelRedGreenBlueReserved8BitPerColor  ||
            fmt == PixelBlueGreenRedReserved8BitPerColor  ||
            fmt == PixelBitMask) &&
           Gop->Mode->FrameBufferBase != 0;
}

static void BltFillRect(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT8 r, UINT8 g, UINT8 b)
{
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = { b, g, r, 0 };
    uefi_call_wrapper(Gop->Blt, 10, Gop, &px, EfiBltVideoFill, 0, 0, X, Y, W, H, 0);
}

void FillScreen(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 Color) {
    UINT8 r = (UINT8)((Color >> 16) & 0xFF);
    UINT8 g = (UINT8)((Color >> 8)  & 0xFF);
    UINT8 b = (UINT8)((Color >> 0)  & 0xFF);

    if (IsFrameBufferDirect(Gop)) {
        UINT32 *FrameBuffer = (UINT32 *)Gop->Mode->FrameBufferBase;
        UINT32  Width       = Gop->Mode->Info->HorizontalResolution;
        UINT32  Height      = Gop->Mode->Info->VerticalResolution;
        UINT32  Pitch       = Gop->Mode->Info->PixelsPerScanLine;
        for (UINT32 y = 0; y < Height; y++)
            for (UINT32 x = 0; x < Width; x++)
                FrameBuffer[y * Pitch + x] = Color;
    } else {
        BltFillRect(Gop, 0, 0,
            Gop->Mode->Info->HorizontalResolution,
            Gop->Mode->Info->VerticalResolution,
            r, g, b);
    }
}

static EFI_STATUS DisplayBMPFromBuffer(
    EFI_SYSTEM_TABLE             *ST,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    VOID                         *Buffer,
    UINTN                         Size
) {
    BMP_FILE_HEADER *FileHdr = (BMP_FILE_HEADER *)Buffer;
    BMP_INFO_HEADER *InfoHdr = (BMP_INFO_HEADER *)((UINT8 *)Buffer + sizeof(BMP_FILE_HEADER));

    if (Size < sizeof(BMP_FILE_HEADER) + sizeof(BMP_INFO_HEADER)) return EFI_LOAD_ERROR;
    if (FileHdr->bfType != 0x4D42) return EFI_LOAD_ERROR;

    UINT8   *PixelData    = (UINT8 *)Buffer + FileHdr->bfOffBits;
    UINT32   width        = (UINT32)InfoHdr->biWidth;
    INT32    heightSigned = InfoHdr->biHeight;
    UINT32   height       = (heightSigned > 0) ? (UINT32)heightSigned : (UINT32)(-heightSigned);
    BOOLEAN  TopDown      = (heightSigned < 0);
    UINT32   bpp          = InfoHdr->biBitCount;

    if (bpp != 24 && bpp != 32) return EFI_UNSUPPORTED;

    UINT32   ScreenWidth  = Gop->Mode->Info->HorizontalResolution;
    UINT32   ScreenHeight = Gop->Mode->Info->VerticalResolution;
    UINT32   StartX       = (ScreenWidth  > width)  ? (ScreenWidth  - width)  / 2 : 0;
    UINT32   StartY       = (ScreenHeight > height)  ? (ScreenHeight - height) / 2 : 0;
    UINT32   RowSize      = ((width * (bpp / 8) + 3) & ~3);
    BOOLEAN  UseDirect    = IsFrameBufferDirect(Gop);
    UINT32  *FrameBuffer  = UseDirect ? (UINT32 *)Gop->Mode->FrameBufferBase : NULL;
    UINT32   Pitch        = Gop->Mode->Info->PixelsPerScanLine;

    for (UINT32 y = 0; y < height; y++) {
        UINT32 srcY = TopDown ? y : (height - 1 - y);
        UINT8 *Row  = PixelData + srcY * RowSize;
        for (UINT32 x = 0; x < width; x++) {
            UINT8 b_val = Row[x * (bpp / 8) + 0];
            UINT8 g_val = Row[x * (bpp / 8) + 1];
            UINT8 r_val = Row[x * (bpp / 8) + 2];
            UINT8 a_val = (bpp == 32) ? Row[x * (bpp / 8) + 3] : 0xFF;
            if (a_val == 0) continue;
            if (UseDirect) {
                UINT32 dstColor = FrameBuffer[(StartY + y) * Pitch + (StartX + x)];
                FrameBuffer[(StartY + y) * Pitch + (StartX + x)] =
                    AlphaBlend(dstColor, r_val, g_val, b_val, a_val);
            } else {
                if (a_val == 0xFF) {
                    EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = { b_val, g_val, r_val, 0 };
                    uefi_call_wrapper(Gop->Blt, 10, Gop, &px, EfiBltVideoFill,
                        0, 0, StartX + x, StartY + y, 1, 1, 0);
                }
            }
        }
    }
    return EFI_SUCCESS;
}

void DrawTextGraySmallCenterBottom(
    EFI_SYSTEM_TABLE             *ST,
    EFI_FILE_PROTOCOL            *Root,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,
    const char                   *Text,
    VOID                         *FontBuffer,
    UINTN                         FontSize
) {
    if (!FontBuffer || FontSize == 0) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, (unsigned char *)FontBuffer,
            stbtt_GetFontOffsetForIndex((unsigned char *)FontBuffer, 0))) {
        return;
    }

    float scale   = stbtt_ScaleForPixelHeight(&font, 26.0f);
    int   ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    ascent = (int)(ascent * scale);

    int text_width = 0;
    for (int i = 0; Text[i]; i++) {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, Text[i], &advance, &lsb);
        text_width += (int)(advance * scale);
        if (Text[i + 1])
            text_width += (int)(stbtt_GetCodepointKernAdvance(&font, Text[i], Text[i + 1]) * scale);
    }

    UINT32   ScreenWidth  = Gop->Mode->Info->HorizontalResolution;
    UINT32   ScreenHeight = Gop->Mode->Info->VerticalResolution;
    UINT32   Pitch        = Gop->Mode->Info->PixelsPerScanLine;
    UINT32  *FrameBuffer  = IsFrameBufferDirect(Gop) ? (UINT32 *)Gop->Mode->FrameBufferBase : NULL;
    BOOLEAN  UseDirect    = (FrameBuffer != NULL);

    int start_x = ((int)ScreenWidth - text_width) / 2;
    int start_y = (int)ScreenHeight - 40 - ascent;
    int x       = start_x;

    for (int i = 0; Text[i]; i++) {
        int advance, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(&font, Text[i], &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&font, Text[i], scale, scale, &x0, &y0, &x1, &y1);

        int char_width  = x1 - x0;
        int char_height = y1 - y0;

        if (char_width > 0 && char_height > 0) {
            UINT8      *bitmap = NULL;
            EFI_STATUS  Status = uefi_call_wrapper(ST->BootServices->AllocatePool, 3,
                EfiLoaderData, (UINTN)(char_width * char_height), (VOID **)&bitmap);
            if (!EFI_ERROR(Status)) {
                stbtt_MakeCodepointBitmap(&font, bitmap,
                    char_width, char_height, char_width, scale, scale, Text[i]);

                for (int yy = 0; yy < char_height; yy++) {
                    for (int xx = 0; xx < char_width; xx++) {
                        UINT8 alpha  = bitmap[yy * char_width + xx];
                        if (alpha == 0) continue;
                        int draw_x = x + x0 + xx;
                        int draw_y = start_y + ascent + y0 + yy;
                        if (draw_x < 0 || draw_x >= (int)ScreenWidth  ||
                            draw_y < 0 || draw_y >= (int)ScreenHeight) continue;

                        if (UseDirect) {
                            UINT32 dstColor = FrameBuffer[draw_y * (int)Pitch + draw_x];
                            FrameBuffer[draw_y * (int)Pitch + draw_x] =
                                AlphaBlend(dstColor, 160, 160, 160, alpha);
                        } else {
                            UINT8 blended = (UINT8)((160 * alpha) / 255);
                            EFI_GRAPHICS_OUTPUT_BLT_PIXEL px = { blended, blended, blended, 0 };
                            uefi_call_wrapper(Gop->Blt, 10, Gop, &px, EfiBltVideoFill,
                                0, 0, (UINTN)draw_x, (UINTN)draw_y, 1, 1, 0);
                        }
                    }
                }
                uefi_call_wrapper(ST->BootServices->FreePool, 1, bitmap);
            }
        }

        x += (int)(advance * scale);
        if (Text[i + 1])
            x += (int)(stbtt_GetCodepointKernAdvance(&font, Text[i], Text[i + 1]) * scale);
    }
}

typedef void (*KernelEntryFn)(BOOT_INFO *);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    InitializeLib(ImageHandle, ST);
    bootmanager_libc_init(ST);

    EFI_LOADED_IMAGE_PROTOCOL        *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs          = NULL;
    EFI_FILE_PROTOCOL                *Root        = NULL;
    EFI_FILE_PROTOCOL                *KernelRoot  = NULL;
    EFI_FILE_PROTOCOL                *KernelFile  = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL     *Gop         = NULL;

    VOID   *KernelBuffer = NULL;
    UINTN   KernelSize   = 0;
    UINT64  KernelEntry  = 0;

    BOOT_INFO BootInfo = {0};
    BOOTLOADER_HANDOFF *Handoff = FindBootloaderHandoff(ST);

    EFI_STATUS Status = uefi_call_wrapper(
        ST->BootServices->HandleProtocol, 3,
        ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage) return Status;

    if (LoadedImage->DeviceHandle) {
        Status = uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
            LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
        if (!EFI_ERROR(Status) && Fs) {
            uefi_call_wrapper(Fs->OpenVolume, 2, Fs, &Root);
        }
    }

    Status = uefi_call_wrapper(ST->BootServices->LocateProtocol, 3,
        &gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
    if (EFI_ERROR(Status) || !Gop) return Status;

    FillScreen(Gop, 0x000000);

    VOID  *FontBuffer = NULL;
    UINTN  FontSize   = 0;

    {
        VOID    *BmpBuf  = NULL;
        UINTN    BmpSize = 0;
        BOOLEAN  loaded  = FALSE;

        if (Root) {
            EFI_FILE_PROTOCOL *F = NULL;
            if (!EFI_ERROR(uefi_call_wrapper(Root->Open, 5, Root, &F,
                    L"BootManager\\Resource\\Images\\BootLogo.bmp",
                    EFI_FILE_MODE_READ, 0))) {
                if (!EFI_ERROR(LoadFileToMemory(ST, F, &BmpBuf, &BmpSize)))
                    loaded = TRUE;
                uefi_call_wrapper(F->Close, 1, F);
            }
        }
        if (!loaded) {
            if (!EFI_ERROR(LoadFromISO(ST,
                    "BootManager/Resource/Images/BootLogo.bmp",
                    &BmpBuf, &BmpSize)))
                loaded = TRUE;
        }

        if (loaded && BmpBuf) {
            DisplayBMPFromBuffer(ST, Gop, BmpBuf, BmpSize);
            uefi_call_wrapper(ST->BootServices->FreePool, 1, BmpBuf);
        }
    }

    {
        BOOLEAN loaded = FALSE;

        if (Root) {
            EFI_FILE_PROTOCOL *F = NULL;
            if (!EFI_ERROR(uefi_call_wrapper(Root->Open, 5, Root, &F,
                    L"BootManager\\Resource\\Fonts\\NotoSansJP-Regular.ttf",
                    EFI_FILE_MODE_READ, 0))) {
                if (!EFI_ERROR(LoadFileToMemory(ST, F, &FontBuffer, &FontSize)))
                    loaded = TRUE;
                uefi_call_wrapper(F->Close, 1, F);
            }
        }

        if (!loaded) {
            LoadFromISO(ST,
                "BootManager/Resource/Fonts/NotoSansJP-Regular.ttf",
                &FontBuffer, &FontSize);
        }
    }

    char DisplayText[256] = {0};
    AppendString(DisplayText, "CPU: ");
    AppendString(DisplayText, (Handoff && Handoff->CPUName[0]) ? Handoff->CPUName : "Unknown");
    AppendString(DisplayText, " | Maker: ");
    AppendString(DisplayText, (Handoff && Handoff->Manufacturer[0]) ? Handoff->Manufacturer : "Unknown");
    AppendString(DisplayText, " | Model: ");
    AppendString(DisplayText, (Handoff && Handoff->ProductName[0]) ? Handoff->ProductName : "Unknown");

    if (FontBuffer && FontSize > 0) {
        DrawTextGraySmallCenterBottom(ST, NULL, Gop, DisplayText, FontBuffer, FontSize);
    }

    BootInfo.FontDataAddress = (uint64_t)(UINTN)FontBuffer;
    BootInfo.FontDataSize    = (uint64_t)FontSize;

    BootInfo.FrameBufferBase      = Gop->Mode->FrameBufferBase;
    BootInfo.FrameBufferSize      = Gop->Mode->FrameBufferSize;
    BootInfo.HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
    BootInfo.VerticalResolution   = Gop->Mode->Info->VerticalResolution;
    BootInfo.PixelsPerScanLine    = Gop->Mode->Info->PixelsPerScanLine;

    if (Handoff) {
        BootInfo.PartitionStartLBA = Handoff->PartitionStartLBA;
        BootInfo.BootDriveType     = Handoff->BootDriveType;
        BootInfo.AcpiRsdpAddress   = Handoff->AcpiRsdpAddress;
        BootInfo.AcpiRsdpSize      = Handoff->AcpiRsdpSize;
        BootInfo.AcpiRsdpRevision  = Handoff->AcpiRsdpRevision;
    } else {
        BootInfo.PartitionStartLBA = GetPartitionStartLBA(LoadedImage->DeviceHandle, ST, &BootInfo);
        DiscoverAcpiRsdp(ST, &BootInfo);
    }

    if (BootInfo.BootDriveType == BOOT_DRIVE_TYPE_UNKNOWN &&
        BootInfo.PartitionStartLBA != 0) {
        BootInfo.BootDriveType = BOOT_DRIVE_TYPE_IDE;
    }

    KernelRoot = NULL;

    if (Root != NULL) {
        Status = uefi_call_wrapper(Root->Open, 5, Root, &KernelFile,
            L"Kernel\\Kernel_Main.ELF", EFI_FILE_MODE_READ, 0);
    } else {
        Status = EFI_NOT_FOUND;
    }

    if (EFI_ERROR(Status)) {
        UINTN       Count   = 0;
        EFI_HANDLE *Handles = NULL;
        Status = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
            ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles);
        if (!EFI_ERROR(Status) && Handles) {
            for (UINTN i = 0; i < Count; i++) {
                EFI_FILE_PROTOCOL *AltRoot = OpenFsRootFromHandle(Handles[i], ST);
                if (!AltRoot) continue;
                Status = uefi_call_wrapper(AltRoot->Open, 5, AltRoot, &KernelFile,
                    L"Kernel\\Kernel_Main.ELF", EFI_FILE_MODE_READ, 0);
                if (!EFI_ERROR(Status)) {
                    KernelRoot = AltRoot;
                    break;
                } else {
                    uefi_call_wrapper(AltRoot->Close, 1, AltRoot);
                }
            }
            uefi_call_wrapper(ST->BootServices->FreePool, 1, Handles);
        }
    } else {
        KernelRoot = Root;
    }

    if (KernelRoot && !EFI_ERROR(Status)) {
        Status = LoadFileToMemoryBelow4G(ST, KernelFile, &KernelBuffer, &KernelSize);
        uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
    } else {
        Status = LoadFromISO(ST, "Kernel/Kernel_Main.ELF", &KernelBuffer, &KernelSize);
    }

    if (EFI_ERROR(Status)) {
        while (1) __asm__ volatile("cli; hlt");
    }

    Status = LoadKernelELF(ST, KernelBuffer, KernelSize, &KernelEntry);
    if (EFI_ERROR(Status) || KernelEntry == 0) {
        while (1) __asm__ volatile("cli; hlt");
    }

    uefi_call_wrapper(ST->BootServices->FreePages, 2,
        (EFI_PHYSICAL_ADDRESS)(UINTN)KernelBuffer, EFI_SIZE_TO_PAGES(KernelSize));

    if (KernelRoot) {
        Status = PreloadDriverModules(ST, KernelRoot, &BootInfo);
    } else {
        Status = PreloadDriversFromISO(ST, &BootInfo);
    }
    if (EFI_ERROR(Status)) {
        while (1) __asm__ volatile("cli; hlt");
    }

    Status = ExitBootServicesComplete(ImageHandle, ST, &BootInfo);
    if (EFI_ERROR(Status)) {
        while (1) __asm__ volatile("cli; hlt");
    }

    KernelEntryFn Entry = (KernelEntryFn)KernelEntry;
    Entry(&BootInfo);

    return EFI_SUCCESS;
}