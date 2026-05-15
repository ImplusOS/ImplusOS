#include "FAT32.h"
#include "BootManager_libc/include/string.h"
#include "BootManager_libc/include/stdlib.h"
#include <efilib.h>

#define FAT32_EOC 0x0FFFFFF8u

#pragma pack(push, 1)
typedef struct {
    char Name[11];
    uint8_t Attr;
    uint8_t Reserved;
    uint8_t CreateTimeTenths;
    uint16_t CreateTime;
    uint16_t CreateDate;
    uint16_t LastAccessDate;
    uint16_t FirstClusterHigh;
    uint16_t WriteTime;
    uint16_t WriteDate;
    uint16_t FirstClusterLow;
    uint32_t FileSize;
} FAT32_DIR_ENTRY;
#pragma pack(pop)

static uint32_t cluster_first_sector(BOOTMANAGER_FAT32 *fs, uint32_t cluster) {
    return fs->FirstDataSector + ((cluster - 2u) * fs->BPB.sectors_per_cluster);
}

static EFI_STATUS read_sector(BOOTMANAGER_FAT32 *fs, uint64_t sector, void *buffer) {
    return uefi_call_wrapper(
        fs->BlockIo->ReadBlocks, 5,
        fs->BlockIo,
        fs->BlockIo->Media->MediaId,
        fs->PartitionStartLBA + sector,
        fs->BPB.bytes_per_sector,
        buffer);
}

static EFI_STATUS read_fat_entry(BOOTMANAGER_FAT32 *fs, uint32_t cluster, uint32_t *next_cluster) {
    uint32_t fat_offset = cluster * 4u;
    uint32_t sector = fs->BPB.reserved_sectors + (fat_offset / fs->BPB.bytes_per_sector);
    uint32_t offset = fat_offset % fs->BPB.bytes_per_sector;
    uint8_t *buf = malloc(fs->BPB.bytes_per_sector);
    if (!buf) return EFI_OUT_OF_RESOURCES;
    EFI_STATUS status = read_sector(fs, sector, buf);
    if (!EFI_ERROR(status)) {
        uint32_t value = *(uint32_t *)(buf + offset);
        *next_cluster = value & 0x0FFFFFFFu;
    }
    free(buf);
    return status;
}

static void make_short_name(const char *component, char out[11]) {
    memset(out, ' ', 11);
    int pos = 0;
    int ext = 8;
    int in_ext = 0;
    for (int i = 0; component[i] && component[i] != '/'; ++i) {
        char c = component[i];
        if (c == '.') {
            in_ext = 1;
            continue;
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!in_ext && pos < 8) out[pos++] = c;
        else if (in_ext && ext < 11) out[ext++] = c;
    }
}

static int short_name_matches(const FAT32_DIR_ENTRY *entry, const char *component) {
    char wanted[11];
    make_short_name(component, wanted);
    return memcmp(entry->Name, wanted, 11) == 0;
}

static uint32_t entry_cluster(const FAT32_DIR_ENTRY *entry) {
    return ((uint32_t)entry->FirstClusterHigh << 16) | entry->FirstClusterLow;
}

static EFI_STATUS find_in_directory(
    BOOTMANAGER_FAT32 *fs,
    uint32_t dir_cluster,
    const char *component,
    FAT32_DIR_ENTRY *out_entry
) {
    uint8_t *cluster_buf = malloc(fs->BytesPerCluster);
    if (!cluster_buf) return EFI_OUT_OF_RESOURCES;

    EFI_STATUS status = EFI_NOT_FOUND;
    uint32_t cluster = dir_cluster;
    while (cluster < FAT32_EOC) {
        uint32_t first_sector = cluster_first_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->BPB.sectors_per_cluster; ++s) {
            status = read_sector(fs, first_sector + s, cluster_buf + (s * fs->BPB.bytes_per_sector));
            if (EFI_ERROR(status)) goto done;
        }

        FAT32_DIR_ENTRY *entries = (FAT32_DIR_ENTRY *)cluster_buf;
        UINTN entry_count = fs->BytesPerCluster / sizeof(FAT32_DIR_ENTRY);
        for (UINTN i = 0; i < entry_count; ++i) {
            if ((uint8_t)entries[i].Name[0] == 0x00) {
                status = EFI_NOT_FOUND;
                goto done;
            }
            if ((uint8_t)entries[i].Name[0] == 0xE5 || entries[i].Attr == 0x0F) continue;
            if (short_name_matches(&entries[i], component)) {
                memcpy(out_entry, &entries[i], sizeof(*out_entry));
                status = EFI_SUCCESS;
                goto done;
            }
        }

        status = read_fat_entry(fs, cluster, &cluster);
        if (EFI_ERROR(status)) goto done;
    }
    status = EFI_NOT_FOUND;

done:
    free(cluster_buf);
    return status;
}

EFI_STATUS bootmanager_fat32_init(
    BOOTMANAGER_FAT32 *fs,
    EFI_SYSTEM_TABLE *system_table,
    EFI_HANDLE device_handle,
    uint64_t partition_start_lba,
    const FAT32_BPB *bpb
) {
    if (!fs || !system_table || !device_handle || !bpb || bpb->bytes_per_sector == 0) {
        return EFI_INVALID_PARAMETER;
    }
    memset(fs, 0, sizeof(*fs));
    EFI_STATUS status = uefi_call_wrapper(
        system_table->BootServices->HandleProtocol, 3,
        device_handle, &gEfiBlockIoProtocolGuid, (VOID **)&fs->BlockIo);
    if (EFI_ERROR(status) || !fs->BlockIo) return status;

    fs->SystemTable = system_table;
    fs->BPB = *bpb;
    fs->PartitionStartLBA = partition_start_lba;
    fs->FirstDataSector = fs->BPB.reserved_sectors + (fs->BPB.num_fats * fs->BPB.fat_size_sectors);
    fs->BytesPerCluster = fs->BPB.bytes_per_sector * fs->BPB.sectors_per_cluster;
    return EFI_SUCCESS;
}

EFI_STATUS bootmanager_fat32_read_file(
    BOOTMANAGER_FAT32 *fs,
    const char *path,
    void **buffer,
    UINTN *size
) {
    if (!fs || !path || !buffer || !size) return EFI_INVALID_PARAMETER;
    *buffer = NULL;
    *size = 0;

    uint32_t dir_cluster = fs->BPB.root_cluster;
    const char *component = path;
    while (*component == '/') ++component;

    FAT32_DIR_ENTRY entry;
    while (*component) {
        char current[64];
        UINTN n = 0;
        while (component[n] && component[n] != '/' && n + 1 < sizeof(current)) {
            current[n] = component[n];
            ++n;
        }
        current[n] = 0;
        EFI_STATUS status = find_in_directory(fs, dir_cluster, current, &entry);
        if (EFI_ERROR(status)) return status;
        component += n;
        while (*component == '/') ++component;
        if (*component) {
            if ((entry.Attr & 0x10u) == 0) return EFI_NOT_FOUND;
            dir_cluster = entry_cluster(&entry);
        }
    }

    if (entry.Attr & 0x10u) return EFI_NOT_FOUND;

    void *data = malloc(entry.FileSize);
    if (!data) return EFI_OUT_OF_RESOURCES;
    uint8_t *dst = (uint8_t *)data;
    UINTN remaining = entry.FileSize;
    uint32_t cluster = entry_cluster(&entry);
    while (remaining > 0 && cluster < FAT32_EOC) {
        uint8_t *cluster_buf = malloc(fs->BytesPerCluster);
        if (!cluster_buf) {
            free(data);
            return EFI_OUT_OF_RESOURCES;
        }
        uint32_t first_sector = cluster_first_sector(fs, cluster);
        for (uint32_t s = 0; s < fs->BPB.sectors_per_cluster; ++s) {
            EFI_STATUS status = read_sector(fs, first_sector + s, cluster_buf + (s * fs->BPB.bytes_per_sector));
            if (EFI_ERROR(status)) {
                free(cluster_buf);
                free(data);
                return status;
            }
        }
        UINTN copy_size = remaining < fs->BytesPerCluster ? remaining : fs->BytesPerCluster;
        memcpy(dst, cluster_buf, copy_size);
        dst += copy_size;
        remaining -= copy_size;
        free(cluster_buf);
        EFI_STATUS status = read_fat_entry(fs, cluster, &cluster);
        if (EFI_ERROR(status)) {
            free(data);
            return status;
        }
    }

    *buffer = data;
    *size = entry.FileSize;
    return EFI_SUCCESS;
}
