#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include "../../../BootManager/Handoff.h"
#include "../../../BootManager/ISO9660.h"

#define ISO_SECTOR_SIZE         2048U
#define MAX_MEDIA_BLOCK_SIZE    4096U
#define ISO_TO_ATA(iso_lba)     ((UINT64)(iso_lba) * 4ULL)

STATIC EFI_GUID mAcpi10TableGuid  = { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
STATIC EFI_GUID mAcpi20TableGuid  = { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
STATIC EFI_GUID mSmbiosTableGuid  = { 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
STATIC EFI_GUID mSmbios3TableGuid = { 0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } };

#pragma pack(push, 1)
typedef struct {
  CHAR8   Signature[8];
  UINT8   Checksum;
  CHAR8   OemId[6];
  UINT8   Revision;
  UINT32  RsdtAddress;
} ACPI_RSDP_V1;

typedef struct {
  ACPI_RSDP_V1  V1;
  UINT32        Length;
  UINT64        XsdtAddress;
  UINT8         ExtendedChecksum;
  UINT8         Reserved[3];
} ACPI_RSDP_V2;

typedef struct {
  UINT8   AnchorString[4];
  UINT8   EntryPointStructureChecksum;
  UINT8   EntryPointLength;
  UINT8   SMBIOSMajorVersion;
  UINT8   SMBIOSMinorVersion;
  UINT16  MaxStructureSize;
  UINT8   EntryPointRevision;
  UINT8   FormattedArea[5];
  UINT8   IntermediateAnchorString[5];
  UINT8   IntermediateChecksum;
  UINT16  StructureTableLength;
  UINT32  StructureTableAddress;
  UINT16  NumberOfSMBIOSStructures;
  UINT8   SMBIOSBCDRevision;
} SMBIOS_EPS;

typedef struct {
  UINT8   AnchorString[5];
  UINT8   EntryPointStructureChecksum;
  UINT8   EntryPointLength;
  UINT8   SMBIOSMajorVersion;
  UINT8   SMBIOSMinorVersion;
  UINT8   SMBIOSDocrev;
  UINT8   EntryPointRevision;
  UINT8   Reserved;
  UINT32  StructureTableMaximumSize;
  UINT64  StructureTableAddress;
} SMBIOS3_EPS;

typedef struct {
  UINT8   Type;
  UINT8   Length;
  UINT16  Handle;
} UEFI_SMBIOS_HEADER;
#pragma pack(pop)

STATIC
UINT32
ReadLe32 (
  IN CONST UINT8 *P
  )
{
  if (P == NULL) {
    return 0;
  }

  return (UINT32)P[0] |
         ((UINT32)P[1] << 8) |
         ((UINT32)P[2] << 16) |
         ((UINT32)P[3] << 24);
}

STATIC
VOID
TrimString (
  IN OUT CHAR8 *Str
  )
{
  CHAR8 *Start;
  UINTN  Len;

  if ((Str == NULL) || (*Str == '\0')) {
    return;
  }

  Start = Str;
  while (*Start == ' ') {
    Start++;
  }

  if (Start != Str) {
    while (*Start != '\0') {
      *Str++ = *Start++;
    }
    *Str = '\0';
  }

  Len = AsciiStrLen (Str);
  while ((Len > 0) && (Str[Len - 1] == ' ')) {
    Str[--Len] = '\0';
  }
}

STATIC
CHAR8 *
GetSmbiosString (
  IN UEFI_SMBIOS_HEADER *Hdr,
  IN UINT8              Index,
  IN UINT8             *End
  )
{
  CHAR8 *Str;
  UINT8  I;

  if (Index == 0) {
    return NULL;
  }

  Str = (CHAR8 *)Hdr + Hdr->Length;
  for (I = 1; I < Index; I++) {
    while (((UINT8 *)Str < End) && (*Str != '\0')) {
      Str++;
    }
    Str++;
    if (((UINT8 *)Str >= End) || (*Str == '\0')) {
      return NULL;
    }
  }

  return ((UINT8 *)Str < End) ? Str : NULL;
}

STATIC
VOID
CopySmbiosString (
  OUT CHAR8       *Dst,
  IN  CONST CHAR8  *Src,
  IN  UINT8       *End
  )
{
  UINTN Len;

  if ((Dst == NULL) || (Dst[0] != '\0') || (Src == NULL)) {
    return;
  }

  Len = 0;
  while ((Src[Len] != '\0') &&
         (Len < 63) &&
         ((UINT8 *)&Src[Len] < End))
  {
    Len++;
  }

  CopyMem (Dst, Src, Len);
  Dst[Len] = '\0';
  TrimString (Dst);
}

STATIC
EFI_STATUS
ReadIsoSector (
  IN  EFI_BLOCK_IO_PROTOCOL  *Bio,
  IN  UINT64                  IsoSector,
  IN  VOID                   *Scratch,
  IN  UINTN                   ScratchSize,
  OUT VOID                  **SectorData
  )
{
  UINT32 BlockSize;
  UINT64 BlockLba;
  UINTN  ReadSize;
  UINTN  Offset;
  EFI_STATUS Status;

  if ((Bio == NULL) || (Bio->Media == NULL) || (Scratch == NULL) || (SectorData == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = Bio->Media->BlockSize;
  if ((BlockSize < 512U) || (BlockSize > MAX_MEDIA_BLOCK_SIZE) || (ScratchSize < BlockSize)) {
    return EFI_UNSUPPORTED;
  }

  Offset = 0;
  if (BlockSize > ISO_SECTOR_SIZE) {
    UINTN SectorsPerBlock = BlockSize / ISO_SECTOR_SIZE;
    if (SectorsPerBlock == 0) {
      return EFI_UNSUPPORTED;
    }

    BlockLba = IsoSector / SectorsPerBlock;
    Offset   = (UINTN)((IsoSector % SectorsPerBlock) * ISO_SECTOR_SIZE);
    ReadSize  = BlockSize;
  } else {
    BlockLba = (IsoSector * ISO_SECTOR_SIZE) / BlockSize;
    ReadSize  = ISO_SECTOR_SIZE;
  }

  Status = Bio->ReadBlocks (
                     Bio,
                     Bio->Media->MediaId,
                     BlockLba,
                     ReadSize,
                     Scratch
                     );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *SectorData = (UINT8 *)Scratch + Offset;
  return EFI_SUCCESS;
}

STATIC
VOID
DiscoverSmbios (
  IN EFI_SYSTEM_TABLE      *SystemTable,
  IN OUT BOOTLOADER_HANDOFF *Handoff
  )
{
  VOID   *Smbios = NULL;
  BOOLEAN IsSmbios3 = FALSE;

  for (UINTN Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
    if (CompareGuid (&SystemTable->ConfigurationTable[Index].VendorGuid, &mSmbios3TableGuid)) {
      Smbios = SystemTable->ConfigurationTable[Index].VendorTable;
      IsSmbios3 = TRUE;
      break;
    }

    if (CompareGuid (&SystemTable->ConfigurationTable[Index].VendorGuid, &mSmbiosTableGuid)) {
      Smbios = SystemTable->ConfigurationTable[Index].VendorTable;
    }
  }

  if (Smbios == NULL) {
    return;
  }

  UINT8  *TableAddr = NULL;
  UINT32  TableLen = 0;
  UINT32  NumStructs = 0;

  if (IsSmbios3) {
    SMBIOS3_EPS *Eps3 = (SMBIOS3_EPS *)Smbios;
    TableAddr  = (UINT8 *)(UINTN)Eps3->StructureTableAddress;
    TableLen   = Eps3->StructureTableMaximumSize;
    NumStructs = 0xFFFFFFFFU;
  } else {
    SMBIOS_EPS *Eps = (SMBIOS_EPS *)Smbios;
    TableAddr  = (UINT8 *)(UINTN)Eps->StructureTableAddress;
    TableLen   = Eps->StructureTableLength;
    NumStructs = Eps->NumberOfSMBIOSStructures;
  }

  if ((TableAddr == NULL) || (TableLen == 0)) {
    return;
  }

  UINT8 *Ptr = TableAddr;
  UINT8 *End = TableAddr + TableLen;

  for (UINT32 Count = 0;
       (Ptr + sizeof (UEFI_SMBIOS_HEADER) <= End) && (Count < NumStructs);
       Count++)
  {
    UEFI_SMBIOS_HEADER *Hdr = (UEFI_SMBIOS_HEADER *)Ptr;

    if ((Hdr->Type == 127) || (Hdr->Length < sizeof (UEFI_SMBIOS_HEADER))) {
      break;
    }

    if ((Hdr->Type == 1) && (Hdr->Length >= 6)) {
      CopySmbiosString (Handoff->Manufacturer,
                        GetSmbiosString (Hdr, Ptr[0x04], End),
                        End);
      CopySmbiosString (Handoff->ProductName,
                        GetSmbiosString (Hdr, Ptr[0x05], End),
                        End);
    } else if ((Hdr->Type == 4) && (Hdr->Length >= 0x11)) {
      CopySmbiosString (Handoff->CPUName,
                        GetSmbiosString (Hdr, Ptr[0x10], End),
                        End);
    }

    Ptr += Hdr->Length;
    while (Ptr + 1 < End) {
      if ((Ptr[0] == 0) && (Ptr[1] == 0)) {
        Ptr += 2;
        break;
      }
      Ptr++;
    }
  }
}

STATIC
VOID
DiscoverAcpiRsdp (
  IN EFI_SYSTEM_TABLE      *SystemTable,
  IN OUT BOOTLOADER_HANDOFF *Handoff
  )
{
  for (UINTN Pass = 0; Pass < 2; Pass++) {
    EFI_GUID *Target = (Pass == 0) ? &mAcpi20TableGuid : &mAcpi10TableGuid;

    for (UINTN Index = 0; Index < SystemTable->NumberOfTableEntries; Index++) {
      EFI_CONFIGURATION_TABLE *Cfg = &SystemTable->ConfigurationTable[Index];

      if ((Cfg->VendorTable == NULL) ||
          !CompareGuid (&Cfg->VendorGuid, Target))
      {
        continue;
      }

      CONST CHAR8 *Sig = (CONST CHAR8 *)Cfg->VendorTable;
      if ((Sig[0] != 'R') || (Sig[1] != 'S') || (Sig[2] != 'D') || (Sig[3] != ' ') ||
          (Sig[4] != 'P') || (Sig[5] != 'T') || (Sig[6] != 'R') || (Sig[7] != ' '))
      {
        continue;
      }

      ACPI_RSDP_V1 *V1 = (ACPI_RSDP_V1 *)Cfg->VendorTable;
      Handoff->AcpiRsdpAddress  = (UINT64)(UINTN)Cfg->VendorTable;
      Handoff->AcpiRsdpRevision = V1->Revision;
      Handoff->AcpiRsdpSize     = (UINT32)sizeof (ACPI_RSDP_V1);

      if (V1->Revision >= 2) {
        ACPI_RSDP_V2 *V2 = (ACPI_RSDP_V2 *)Cfg->VendorTable;
        if (V2->Length > 0) {
          Handoff->AcpiRsdpSize = V2->Length;
        }
      }

      return;
    }
  }
}

STATIC
UINT64
ParseElToritoCatalog (
  IN EFI_BLOCK_IO_PROTOCOL *Bio
  )
{
  EFI_STATUS Status;
  VOID       *Scratch = NULL;
  VOID       *Sector = NULL;
  UINT32      BlockSize;
  UINT64      Result = 0;

  if ((Bio == NULL) || (Bio->Media == NULL)) {
    return 0;
  }

  BlockSize = Bio->Media->BlockSize;
  if ((BlockSize < 512U) || (BlockSize > MAX_MEDIA_BLOCK_SIZE)) {
    return 0;
  }

  Status = gBS->AllocatePool (EfiLoaderData, MAX_MEDIA_BLOCK_SIZE, &Scratch);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  Status = ReadIsoSector (Bio, 16, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
  if (!EFI_ERROR (Status)) {
    UINT8 *Buf = (UINT8 *)Sector;

    if ((Buf[1] == 'C') && (Buf[2] == 'D') && (Buf[3] == '0') &&
        (Buf[4] == '0') && (Buf[5] == '1'))
    {
      for (UINT32 S = 17; (S < 32) && (Result == 0); S++) {
        Status = ReadIsoSector (Bio, S, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
        if (EFI_ERROR (Status)) {
          continue;
        }

        Buf = (UINT8 *)Sector;
        if ((Buf[0] == 0xFF) || (Buf[0] != 0x00)) {
          continue;
        }

        UINT32 CatIsoLba = ReadLe32 (&Buf[71]);
        if (CatIsoLba == 0) {
          continue;
        }

        Status = ReadIsoSector (Bio, CatIsoLba, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
        if (EFI_ERROR (Status)) {
          continue;
        }

        Buf = (UINT8 *)Sector + 32;
        if ((Buf[0] == 0x88)) {
          UINT32 Rba = ReadLe32 (&Buf[8]);
          if (Rba != 0) {
            Result = ISO_TO_ATA (Rba);
          }
        }
      }
    }
  }

  gBS->FreePool (Scratch);
  return Result;
}

STATIC
UINT64
GetPartitionStartLBA (
  IN EFI_HANDLE           DeviceHandle,
  IN EFI_SYSTEM_TABLE    *SystemTable,
  IN OUT BOOTLOADER_HANDOFF *Handoff,
  OUT BOOLEAN            *OutIsIsoLba
  )
{
  EFI_STATUS Status;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;

  *OutIsIsoLba = FALSE;
  Handoff->BootDriveType = BOOT_DRIVE_TYPE_UNKNOWN;

  DevicePath = NULL;
  Status = gBS->HandleProtocol (
                  DeviceHandle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&DevicePath
                  );
  if (!EFI_ERROR (Status) && (DevicePath != NULL)) {
    EFI_DEVICE_PATH_PROTOCOL *Node = DevicePath;

    while (!IsDevicePathEnd (Node)) {
      if (DevicePathType (Node) == MEDIA_DEVICE_PATH) {
        if (DevicePathSubType (Node) == MSG_USB_DP) {
          Handoff->BootDriveType = BOOT_DRIVE_TYPE_USB;
        } else if ((DevicePathSubType (Node) == MSG_ATAPI_DP) ||
                   (DevicePathSubType (Node) == MSG_SATA_DP))
        {
          Handoff->BootDriveType = BOOT_DRIVE_TYPE_IDE;
        }
      }

      if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
          (DevicePathSubType (Node) == MEDIA_HARDDRIVE_DP))
      {
        HARDDRIVE_DEVICE_PATH *Hd = (HARDDRIVE_DEVICE_PATH *)Node;
        *OutIsIsoLba = FALSE;
        return Hd->PartitionStart;
      }

      Node = NextDevicePathNode (Node);
    }
  }

  EFI_HANDLE *Handles = NULL;
  UINTN      Count = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (!EFI_ERROR (Status) && (Handles != NULL)) {
    for (UINTN Index = 0; Index < Count; Index++) {
      EFI_BLOCK_IO_PROTOCOL *Bio = NULL;

      Status = gBS->HandleProtocol (
                      Handles[Index],
                      &gEfiBlockIoProtocolGuid,
                      (VOID **)&Bio
                      );
      if (EFI_ERROR (Status) || (Bio == NULL) || (Bio->Media == NULL)) {
        continue;
      }

      if (Bio->Media->LogicalPartition || (Bio->Media->LastBlock < 200)) {
        continue;
      }

      UINT64 Lba = ParseElToritoCatalog (Bio);
      if (Lba != 0) {
        gBS->FreePool (Handles);
        *OutIsIsoLba = TRUE;
        return Lba;
      }
    }

    gBS->FreePool (Handles);
  }

  return 0;
}

STATIC
EFI_FILE_PROTOCOL *
OpenFsRootFromHandle (
  IN EFI_HANDLE        Handle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_FILE_PROTOCOL               *Root = NULL;
  EFI_STATUS Status;

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Fs
                  );
  if (EFI_ERROR (Status) || (Fs == NULL)) {
    return NULL;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  return EFI_ERROR (Status) ? NULL : Root;
}

STATIC
EFI_STATUS
TryStartBootManagerFromHandle (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN EFI_HANDLE        DeviceHandle
  )
{
  EFI_FILE_PROTOCOL *Root = NULL;
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_STATUS        Status;
  EFI_DEVICE_PATH   *BootManagerPath;

  Root = OpenFsRootFromHandle (DeviceHandle, SystemTable);
  if (Root == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = Root->Open (
                  Root,
                  &File,
                  L"\\EFI\\BOOT\\BOOTMANAGER.EFI",
                  EFI_FILE_MODE_READ,
                  0
                  );
  Root->Close (Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  File->Close (File);

  BootManagerPath = FileDevicePath (DeviceHandle, L"\\EFI\\BOOT\\BOOTMANAGER.EFI");
  if (BootManagerPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  EFI_HANDLE BootManagerHandle = NULL;
  Status = gBS->LoadImage (
                  FALSE,
                  ImageHandle,
                  BootManagerPath,
                  NULL,
                  0,
                  &BootManagerHandle
                  );
  gBS->FreePool (BootManagerPath);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  UINTN   ExitDataSize = 0;
  CHAR16 *ExitData = NULL;
  return gBS->StartImage (
               BootManagerHandle,
               &ExitDataSize,
               &ExitData
               );
}

STATIC
INTN
AsciiCaseCompare (
  IN CONST CHAR8 *Left,
  IN CONST CHAR8 *Right
  )
{
  CHAR8 C1;
  CHAR8 C2;

  while ((*Left != '\0') && (*Right != '\0')) {
    C1 = *Left;
    C2 = *Right;

    if ((C1 >= 'a') && (C1 <= 'z')) {
      C1 = (CHAR8)(C1 - ('a' - 'A'));
    }
    if ((C2 >= 'a') && (C2 <= 'z')) {
      C2 = (CHAR8)(C2 - ('a' - 'A'));
    }
    if (C1 != C2) {
      break;
    }

    Left++;
    Right++;
  }

  C1 = *Left;
  C2 = *Right;
  if ((C1 >= 'a') && (C1 <= 'z')) {
    C1 = (CHAR8)(C1 - ('a' - 'A'));
  }
  if ((C2 >= 'a') && (C2 <= 'z')) {
    C2 = (CHAR8)(C2 - ('a' - 'A'));
  }

  return (INTN)((UINT8)C1) - (INTN)((UINT8)C2);
}

STATIC
EFI_STATUS
TryStartBootManagerFromISO (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable,
  IN EFI_HANDLE        BlockHandle
  )
{
  EFI_BLOCK_IO_PROTOCOL *Bio = NULL;
  EFI_STATUS           Status;
  VOID                 *Scratch = NULL;
  VOID                 *Sector = NULL;
  UINT8                *Buf;
  UINT32                CurrentLba;
  UINT32                CurrentSize;
  const CHAR8          *Path[] = { "EFI", "BOOT", "BOOTMANAGER.EFI" };

  Status = gBS->HandleProtocol (
                  BlockHandle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **)&Bio
                  );
  if (EFI_ERROR (Status) || (Bio == NULL) || (Bio->Media == NULL)) {
    return EFI_NOT_FOUND;
  }

  Status = gBS->AllocatePool (EfiLoaderData, MAX_MEDIA_BLOCK_SIZE, &Scratch);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ReadIsoSector (Bio, 16, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
  if (EFI_ERROR (Status)) {
    gBS->FreePool (Scratch);
    return EFI_NOT_FOUND;
  }

  Buf = (UINT8 *)Sector;
  if ((Buf[1] != 'C') || (Buf[2] != 'D') || (Buf[3] != '0') ||
      (Buf[4] != '0') || (Buf[5] != '1'))
  {
    gBS->FreePool (Scratch);
    return EFI_NOT_FOUND;
  }

  {
    ISO9660_PVD *Pvd = (ISO9660_PVD *)Buf;
    CurrentLba  = Pvd->root_dir_record.extent_lba_le;
    CurrentSize = Pvd->root_dir_record.data_length_le;
  }

  for (INTN Step = 0; Step < 3; Step++) {
    UINT32 Sectors = (CurrentSize + (ISO_SECTOR_SIZE - 1)) / ISO_SECTOR_SIZE;
    BOOLEAN Found = FALSE;

    if (Sectors > 256) {
      Sectors = 128;
    }

    for (UINT32 S = 0; S < Sectors; S++) {
      Status = ReadIsoSector (Bio, (UINT64)CurrentLba + S, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
      if (EFI_ERROR (Status)) {
        break;
      }

      Buf = (UINT8 *)Sector;
      while (Buf < ((UINT8 *)Sector + ISO_SECTOR_SIZE)) {
        ISO9660_DIR_RECORD *Rec = (ISO9660_DIR_RECORD *)Buf;
        if (Rec->length == 0) {
          break;
        }
        if (Buf + Rec->length > ((UINT8 *)Sector + ISO_SECTOR_SIZE)) {
          break;
        }

        if ((Rec->name_length > 0) && (Rec->name[0] != 0) && (Rec->name[0] != 1)) {
          CHAR8 EntryName[256];
          UINTN Len = Rec->name_length;

          if (Len > 255) {
            Len = 255;
          }

          CopyMem (EntryName, Rec->name, Len);
          EntryName[Len] = '\0';

          for (UINTN I = 0; I < Len; I++) {
            if (EntryName[I] == ';') {
              EntryName[I] = '\0';
              break;
            }
          }

          Len = AsciiStrLen (EntryName);
          if ((Len > 0) && (EntryName[Len - 1] == '.')) {
            EntryName[Len - 1] = '\0';
          }

          if (AsciiCaseCompare (EntryName, Path[Step]) == 0) {
            CurrentLba  = Rec->extent_lba_le;
            CurrentSize = Rec->data_length_le;
            Found = TRUE;
            break;
          }
        }

        Buf += Rec->length;
      }

      if (Found) {
        break;
      }
    }

    if (!Found) {
      gBS->FreePool (Scratch);
      return EFI_NOT_FOUND;
    }
  }

  Status = ReadIsoSector (Bio, CurrentLba, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
  if (EFI_ERROR (Status)) {
    gBS->FreePool (Scratch);
    return EFI_NOT_FOUND;
  }

  Buf = (UINT8 *)Sector;
  if ((Buf[0] != 'M') || (Buf[1] != 'Z')) {
    gBS->FreePool (Scratch);
    return EFI_NOT_FOUND;
  }

  EFI_PHYSICAL_ADDRESS FileAddr = 0;
  UINTN Pages = EFI_SIZE_TO_PAGES (CurrentSize);

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiLoaderCode,
                  Pages,
                  &FileAddr
                  );
  if (EFI_ERROR (Status)) {
    gBS->FreePool (Scratch);
    return Status;
  }

  UINT8 *Dst = (UINT8 *)(UINTN)FileAddr;
  UINT32 Remaining = CurrentSize;

  for (UINT32 IsoLba = CurrentLba; Remaining > 0; IsoLba++) {
    Status = ReadIsoSector (Bio, IsoLba, Scratch, MAX_MEDIA_BLOCK_SIZE, &Sector);
    if (EFI_ERROR (Status)) {
      break;
    }

    Buf = (UINT8 *)Sector;
    UINT32 CopySize = (Remaining < ISO_SECTOR_SIZE) ? Remaining : ISO_SECTOR_SIZE;
    CopyMem (Dst, Buf, CopySize);
    Dst       += CopySize;
    Remaining -= CopySize;
  }

  gBS->FreePool (Scratch);

  if (EFI_ERROR (Status)) {
    gBS->FreePages (FileAddr, Pages);
    return Status;
  }

  EFI_HANDLE BootManagerHandle = NULL;
  Status = gBS->LoadImage (
                  FALSE,
                  ImageHandle,
                  NULL,
                  (VOID *)(UINTN)FileAddr,
                  CurrentSize,
                  &BootManagerHandle
                  );

  gBS->FreePages (FileAddr, Pages);

  if (EFI_ERROR (Status)) {
    return Status;
  }

  UINTN   ExitDataSize = 0;
  CHAR16 *ExitData = NULL;
  return gBS->StartImage (
               BootManagerHandle,
               &ExitDataSize,
               &ExitData
               );
}

STATIC
EFI_STATUS
StartBootManager (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  EFI_STATUS                Status;
  EFI_HANDLE               *Handles = NULL;
  UINTN                     Count = 0;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status) || (LoadedImage == NULL)) {
    return Status;
  }

  Status = TryStartBootManagerFromHandle (
             ImageHandle,
             SystemTable,
             LoadedImage->DeviceHandle
             );
  if (!EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (!EFI_ERROR (Status) && (Handles != NULL)) {
    for (UINTN Index = 0; Index < Count; Index++) {
      if (Handles[Index] == LoadedImage->DeviceHandle) {
        continue;
      }

      Status = TryStartBootManagerFromHandle (ImageHandle, SystemTable, Handles[Index]);
      if (!EFI_ERROR (Status)) {
        gBS->FreePool (Handles);
        return EFI_SUCCESS;
      }
    }
    gBS->FreePool (Handles);
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (!EFI_ERROR (Status) && (Handles != NULL)) {
    for (UINTN Index = 0; Index < Count; Index++) {
      Status = TryStartBootManagerFromISO (ImageHandle, SystemTable, Handles[Index]);
      if (!EFI_ERROR (Status)) {
        gBS->FreePool (Handles);
        return EFI_SUCCESS;
      }
    }
    gBS->FreePool (Handles);
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS          Status;
  BOOTLOADER_HANDOFF  *Handoff = NULL;
  BOOLEAN              IsIsoLba = FALSE;
  EFI_GUID             HandoffGuid = IMPLUSOS_BOOT_HANDOFF_GUID;

  Status = gBS->AllocatePool (
                  EfiLoaderData,
                  sizeof (BOOTLOADER_HANDOFF),
                  (VOID **)&Handoff
                  );
  if (EFI_ERROR (Status) || (Handoff == NULL)) {
    return Status;
  }

  ZeroMem (Handoff, sizeof (*Handoff));
  Handoff->Signature = IMPLUSOS_BOOT_HANDOFF_SIGNATURE;
  Handoff->Version   = IMPLUSOS_BOOT_HANDOFF_VERSION;

  DiscoverSmbios (SystemTable, Handoff);
  DiscoverAcpiRsdp (SystemTable, Handoff);

  {
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    Status = gBS->HandleProtocol (
                    ImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );
    if (EFI_ERROR (Status) || (LoadedImage == NULL)) {
      gBS->FreePool (Handoff);
      return Status;
    }

    Handoff->PartitionStartLBA = GetPartitionStartLBA (
                                   LoadedImage->DeviceHandle,
                                   SystemTable,
                                   Handoff,
                                   &IsIsoLba
                                   );
  }

  Status = gBS->InstallConfigurationTable (&HandoffGuid, Handoff);
  if (EFI_ERROR (Status)) {
    gBS->FreePool (Handoff);
    return Status;
  }

  Status = StartBootManager (ImageHandle, SystemTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}
