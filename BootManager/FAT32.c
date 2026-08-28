#include "FAT32.h"

#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#define FAT32_EOC  0x0FFFFFF8u

#pragma pack(push, 1)
typedef struct {
    CHAR8   Name[11];
    UINT8   Attr;
    UINT8   Reserved;
    UINT8   CreateTimeTenths;
    UINT16  CreateTime;
    UINT16  CreateDate;
    UINT16  LastAccessDate;
    UINT16  FirstClusterHigh;
    UINT16  WriteTime;
    UINT16  WriteDate;
    UINT16  FirstClusterLow;
    UINT32  FileSize;
} FAT32_DIR_ENTRY;
#pragma pack(pop)

STATIC
UINT32
cluster_first_sector(
    IN BOOTMANAGER_FAT32  *fs,
    IN UINT32              cluster
    )
{
    return fs->FirstDataSector + ((cluster - 2U) * fs->BPB.sectors_per_cluster);
}

STATIC
EFI_STATUS
read_sector(
    IN BOOTMANAGER_FAT32  *fs,
    IN UINT64              sector,
    OUT VOID              *buffer
    )
{
    return fs->BlockIo->ReadBlocks(
        fs->BlockIo,
        fs->BlockIo->Media->MediaId,
        fs->PartitionStartLBA + sector,
        (UINTN)fs->BPB.bytes_per_sector,
        buffer
    );
}

STATIC
EFI_STATUS
read_fat_entry(
    IN BOOTMANAGER_FAT32  *fs,
    IN UINT32              cluster,
    OUT UINT32            *next_cluster
    )
{
    UINT32  fat_offset;
    UINT32  sector;
    UINT32  offset;
    UINTN   sector_size;
    UINTN   read_size;
    UINT8  *buf;
    EFI_STATUS status;

    fat_offset  = cluster * 4U;
    sector_size = (UINTN)fs->BPB.bytes_per_sector;
    sector      = fs->BPB.reserved_sectors + (fat_offset / (UINT32)sector_size);
    offset      = fat_offset % (UINT32)sector_size;

    read_size = sector_size;
    if (offset + sizeof(UINT32) > sector_size) {
        read_size = sector_size * 2;
    }

    buf = AllocatePool(read_size);
    if (buf == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }

    status = read_sector(fs, sector, buf);
    if (EFI_ERROR(status)) {
        FreePool(buf);
        return status;
    }

    if (read_size > sector_size) {
        status = read_sector(fs, sector + 1, buf + sector_size);
        if (EFI_ERROR(status)) {
            FreePool(buf);
            return status;
        }
    }

    {
        UINT32 value;
        CopyMem(&value, buf + offset, sizeof(UINT32));
        *next_cluster = value & 0x0FFFFFFFu;
    }

    FreePool(buf);
    return EFI_SUCCESS;
}

STATIC
VOID
make_short_name(
    IN  CONST CHAR8  *component,
    OUT CHAR8         out[11]
    )
{
    UINTN i;
    UINTN pos;
    UINTN ext;
    BOOLEAN in_ext;

    SetMem(out, 11, ' ');

    pos = 0;
    ext = 8;
    in_ext = FALSE;

    for (i = 0; component[i] != '\0' && component[i] != '/'; ++i) {
        CHAR8 c = component[i];

        if (c == '.') {
            in_ext = TRUE;
            continue;
        }

        if (c >= 'a' && c <= 'z') {
            c = (CHAR8)(c - 'a' + 'A');
        }

        if (!in_ext) {
            if (pos < 8) {
                out[pos++] = c;
            }
        } else {
            if (ext < 11) {
                out[ext++] = c;
            }
        }
    }
}

STATIC
BOOLEAN
short_name_matches(
    IN CONST FAT32_DIR_ENTRY  *entry,
    IN CONST CHAR8            *component
    )
{
    CHAR8 wanted[11];

    make_short_name(component, wanted);
    return (CompareMem(entry->Name, wanted, 11) == 0);
}

STATIC
UINT32
entry_cluster(
    IN CONST FAT32_DIR_ENTRY *entry
    )
{
    return ((UINT32)entry->FirstClusterHigh << 16) | entry->FirstClusterLow;
}

STATIC
EFI_STATUS
find_in_directory(
    IN  BOOTMANAGER_FAT32  *fs,
    IN  UINT32              dir_cluster,
    IN  CONST CHAR8        *component,
    OUT FAT32_DIR_ENTRY    *out_entry
    )
{
    UINT8       *cluster_buf;
    EFI_STATUS   status;
    UINT32       cluster;

    cluster_buf = AllocatePool(fs->BytesPerCluster);
    if (cluster_buf == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }

    status = EFI_NOT_FOUND;
    cluster = dir_cluster;

    while (cluster >= 2U && cluster < FAT32_EOC) {
        UINT32 first_sector;
        UINT32 s;
        FAT32_DIR_ENTRY *entries;
        UINTN entry_count;

        first_sector = cluster_first_sector(fs, cluster);

        for (s = 0; s < fs->BPB.sectors_per_cluster; ++s) {
            status = read_sector(
                fs,
                first_sector + s,
                cluster_buf + ((UINTN)s * fs->BPB.bytes_per_sector)
            );
            if (EFI_ERROR(status)) {
                goto Done;
            }
        }

        entries = (FAT32_DIR_ENTRY *)cluster_buf;
        entry_count = fs->BytesPerCluster / sizeof(FAT32_DIR_ENTRY);

        for (UINTN i = 0; i < entry_count; ++i) {
            if ((UINT8)entries[i].Name[0] == 0x00) {
                status = EFI_NOT_FOUND;
                goto Done;
            }

            if ((UINT8)entries[i].Name[0] == 0xE5 || entries[i].Attr == 0x0F) {
                continue;
            }

            if (short_name_matches(&entries[i], component)) {
                CopyMem(out_entry, &entries[i], sizeof(FAT32_DIR_ENTRY));
                status = EFI_SUCCESS;
                goto Done;
            }
        }

        status = read_fat_entry(fs, cluster, &cluster);
        if (EFI_ERROR(status)) {
            goto Done;
        }
    }

    status = EFI_NOT_FOUND;

Done:
    FreePool(cluster_buf);
    return status;
}

EFI_STATUS
bootmanager_fat32_init(
    IN BOOTMANAGER_FAT32   *fs,
    IN EFI_SYSTEM_TABLE    *system_table,
    IN EFI_HANDLE           device_handle,
    IN UINT64               partition_start_lba,
    IN CONST FAT32_BPB     *bpb
    )
{
    EFI_STATUS status;

    if (fs == NULL || system_table == NULL || device_handle == NULL || bpb == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    if (bpb->bytes_per_sector == 0 || bpb->sectors_per_cluster == 0) {
        return EFI_INVALID_PARAMETER;
    }

    ZeroMem(fs, sizeof(*fs));

    status = system_table->BootServices->HandleProtocol(
        device_handle,
        &gEfiBlockIoProtocolGuid,
        (VOID **)&fs->BlockIo
    );
    if (EFI_ERROR(status) || fs->BlockIo == NULL) {
        return status;
    }

    fs->SystemTable        = system_table;
    fs->BPB                = *bpb;
    fs->PartitionStartLBA  = partition_start_lba;
    fs->FirstDataSector    = fs->BPB.reserved_sectors +
                             (fs->BPB.num_fats * fs->BPB.fat_size_sectors);
    fs->BytesPerCluster    = (UINTN)fs->BPB.bytes_per_sector *
                             (UINTN)fs->BPB.sectors_per_cluster;

    return EFI_SUCCESS;
}

EFI_STATUS
bootmanager_fat32_read_file(
    IN  BOOTMANAGER_FAT32  *fs,
    IN  CONST CHAR8        *path,
    OUT VOID              **buffer,
    OUT UINTN              *size
    )
{
    CONST CHAR8     *component;
    FAT32_DIR_ENTRY  entry;
    UINT32           dir_cluster;
    EFI_STATUS       status;

    if (fs == NULL || path == NULL || buffer == NULL || size == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    *buffer = NULL;
    *size   = 0;

    component = path;
    while (*component == '/') {
        ++component;
    }

    if (*component == '\0') {
        return EFI_NOT_FOUND;
    }

    dir_cluster = fs->BPB.root_cluster;

    while (*component != '\0') {
        CHAR8 current[64];
        UINTN n = 0;

        while (component[n] != '\0' && component[n] != '/') {
            if (n + 1 >= sizeof(current)) {
                return EFI_BUFFER_TOO_SMALL;
            }
            current[n] = component[n];
            ++n;
        }
        current[n] = '\0';

        status = find_in_directory(fs, dir_cluster, current, &entry);
        if (EFI_ERROR(status)) {
            return status;
        }

        component += n;
        while (*component == '/') {
            ++component;
        }

        if (*component != '\0') {
            if ((entry.Attr & 0x10U) == 0) {
                return EFI_NOT_FOUND;
            }
            dir_cluster = entry_cluster(&entry);
        }
    }

    if ((entry.Attr & 0x10U) != 0) {
        return EFI_NOT_FOUND;
    }

    *size = (UINTN)entry.FileSize;
    if (*size == 0) {
        return EFI_SUCCESS;
    }

    {
        UINT8  *data;
        UINT8  *dst;
        UINTN   remaining;
        UINT32  cluster;

        data = AllocatePool(*size);
        if (data == NULL) {
            return EFI_OUT_OF_RESOURCES;
        }

        dst = data;
        remaining = *size;
        cluster = entry_cluster(&entry);

        while (remaining > 0 && cluster >= 2U && cluster < FAT32_EOC) {
            UINT8      *cluster_buf;
            UINT32      first_sector;
            UINT32      s;
            EFI_STATUS  read_status;
            UINTN       copy_size;

            cluster_buf = AllocatePool(fs->BytesPerCluster);
            if (cluster_buf == NULL) {
                FreePool(data);
                return EFI_OUT_OF_RESOURCES;
            }

            first_sector = cluster_first_sector(fs, cluster);

            read_status = EFI_SUCCESS;
            for (s = 0; s < fs->BPB.sectors_per_cluster; ++s) {
                read_status = read_sector(
                    fs,
                    first_sector + s,
                    cluster_buf + ((UINTN)s * fs->BPB.bytes_per_sector)
                );
                if (EFI_ERROR(read_status)) {
                    break;
                }
            }

            if (EFI_ERROR(read_status)) {
                FreePool(cluster_buf);
                FreePool(data);
                return read_status;
            }

            copy_size = (remaining < fs->BytesPerCluster) ? remaining : fs->BytesPerCluster;
            CopyMem(dst, cluster_buf, copy_size);

            dst += copy_size;
            remaining -= copy_size;

            FreePool(cluster_buf);

            status = read_fat_entry(fs, cluster, &cluster);
            if (EFI_ERROR(status)) {
                FreePool(data);
                return status;
            }
        }

        if (remaining != 0) {
            FreePool(data);
            return EFI_VOLUME_CORRUPTED;
        }

        *buffer = data;
    }

    return EFI_SUCCESS;
}