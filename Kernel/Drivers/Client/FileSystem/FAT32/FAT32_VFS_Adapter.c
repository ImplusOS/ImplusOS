#include "FAT32_VFS_Adapter.h"
#include "FAT32_Main.h"
#include "kernel/interfaces/vfs_types.h"
#include <string.h>
#include <stdlib.h>

static bool fat32_vfs_find_file(const char *path, vfs_file_t *out_file) {
    FAT32_FILE *fat_file = (FAT32_FILE *)malloc(sizeof(FAT32_FILE));
    if (!fat_file) return false;
    
    if (!fat32_find_file(path, fat_file)) {
        free(fat_file);
        return false;
    }
    
    out_file->internal_id = (uint64_t)fat_file;
    out_file->size = fat_file->size;
    out_file->driver_data = fat_file;
    return true;
}

static bool fat32_vfs_read_file(vfs_file_t *file, uint8_t *buffer) {
    if (!file || !file->driver_data) return false;
    return fat32_read_file((FAT32_FILE *)file->driver_data, buffer);
}

static bool fat32_vfs_write_file(vfs_file_t *file, const uint8_t *buffer) {
    if (!file || !file->driver_data) return false;
    return fat32_write_file((FAT32_FILE *)file->driver_data, buffer);
}

static bool fat32_vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size) {
    if (!file || !file->driver_data) return false;
    return fat32_read_at((FAT32_FILE *)file->driver_data, offset, buffer, size);
}

static bool fat32_vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size) {
    if (!file || !file->driver_data) return false;
    return fat32_write_at((FAT32_FILE *)file->driver_data, offset, buffer, size);
}

static bool fat32_vfs_truncate(vfs_file_t *file, uint32_t new_size) {
    if (!file || !file->driver_data) return false;
    return fat32_truncate((FAT32_FILE *)file->driver_data, new_size);
}

static uint32_t fat32_vfs_get_file_size(vfs_file_t *file) {
    if (!file || !file->driver_data) return 0;
    return fat32_get_file_size((FAT32_FILE *)file->driver_data);
}

static bool fat32_vfs_creat(const char *path) {
    return fat32_creat(path);
}

static bool fat32_vfs_mkdir(const char *path) {
    return fat32_mkdir(path);
}

static int32_t fat32_vfs_opendir(const char *path) {
    return fat32_opendir(path);
}

static int32_t fat32_vfs_readdir(int32_t handle, vfs_dirent_t *out_entry) {
    if (!out_entry) return -1;
    
    FAT32_DIRENT fat_dirent;
    memset(&fat_dirent, 0, sizeof(fat_dirent));
    int32_t result = fat32_readdir(handle, &fat_dirent);
    if (result <= 0) return result;
    
    strncpy(out_entry->name, fat_dirent.name, sizeof(out_entry->name) - 1);
    out_entry->name[sizeof(out_entry->name) - 1] = '\0';
    out_entry->size = fat_dirent.size;
    out_entry->is_directory = (fat_dirent.attributes & 0x10) != 0;
    
    return result;
}

static int32_t fat32_vfs_closedir(int32_t handle) {
    return fat32_closedir(handle);
}

static bool fat32_vfs_unlink(const char *path) {
    return fat32_unlink(path);
}

static bool fat32_vfs_close_file(vfs_file_t *file) {
    if (!file || !file->driver_data) {
        return false;
    }
    free(file->driver_data);
    file->driver_data = NULL;
    return true;
}

static const vfs_driver_t g_fat32_vfs_driver = {
    .fs_type = "fat32",
    .prefix = NULL,
    .find_file = fat32_vfs_find_file,
    .read_file = fat32_vfs_read_file,
    .write_file = fat32_vfs_write_file,
    .read_at = fat32_vfs_read_at,
    .write_at = fat32_vfs_write_at,
    .truncate = fat32_vfs_truncate,
    .get_file_size = fat32_vfs_get_file_size,
    .creat = fat32_vfs_creat,
    .mkdir = fat32_vfs_mkdir,
    .opendir = fat32_vfs_opendir,
    .readdir = fat32_vfs_readdir,
    .closedir = fat32_vfs_closedir,
    .close_file = fat32_vfs_close_file,
    .unlink = fat32_vfs_unlink,
};

const vfs_driver_t *fat32_vfs_get_driver(void) {
    return &g_fat32_vfs_driver;
}