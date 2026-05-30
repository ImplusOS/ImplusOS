#include "ISO9660_VFS_Adapter.h"
#include "ISO9660_Main.h"
#include "kernel/interfaces/vfs_types.h"
#include "Debug/serial/Serial.h"
#include <string.h>
#include <stdlib.h>

static bool iso9660_vfs_find_file(const char *path, vfs_file_t *out_file) {
    ISO9660_FILE *iso_file = (ISO9660_FILE *)malloc(sizeof(ISO9660_FILE));
    if (!iso_file) return false;
    
    if (!iso9660_find_file(path, iso_file)) {
        free(iso_file);
        return false;
    }
    
    out_file->internal_id = (uint64_t)iso_file;
    out_file->size = iso_file->size;
    out_file->driver_data = iso_file;
    return true;
}

static bool iso9660_vfs_read_file(vfs_file_t *file, uint8_t *buffer) {
    if (!file || !file->driver_data) return false;
    return iso9660_read_file((ISO9660_FILE *)file->driver_data, buffer);
}

static bool iso9660_vfs_write_file(vfs_file_t *file, const uint8_t *buffer) {
    (void)file;
    (void)buffer;
    return false;
}

static bool iso9660_vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size) {
    if (!file || !file->driver_data) return false;
    return iso9660_read_at((ISO9660_FILE *)file->driver_data, offset, buffer, size);
}

static bool iso9660_vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size) {
    (void)file;
    (void)offset;
    (void)buffer;
    (void)size;
    return false;
}

static bool iso9660_vfs_truncate(vfs_file_t *file, uint32_t new_size) {
    (void)file;
    (void)new_size;
    return false;
}

static uint32_t iso9660_vfs_get_file_size(vfs_file_t *file) {
    if (!file || !file->driver_data) return 0;
    return iso9660_get_file_size((ISO9660_FILE *)file->driver_data);
}

static bool iso9660_vfs_creat(const char *path) {
    (void)path;
    return false;
}

static bool iso9660_vfs_mkdir(const char *path) {
    (void)path;
    return false;
}

static int32_t iso9660_vfs_opendir(const char *path) {
    return iso9660_opendir(path);
}

static int32_t iso9660_vfs_readdir(int32_t handle, vfs_dirent_t *out_entry) {
    if (!out_entry) return -1;
    
    ISO9660_DIRENT iso_dirent;
    memset(&iso_dirent, 0, sizeof(iso_dirent));
    int32_t result = iso9660_readdir(handle, &iso_dirent);
    if (result <= 0) return result;
    
    strncpy(out_entry->name, iso_dirent.name, sizeof(out_entry->name) - 1);
    out_entry->name[sizeof(out_entry->name) - 1] = '\0';
    out_entry->size = iso_dirent.size;
    out_entry->is_directory = iso_dirent.is_directory != 0;
    
    return result;
}

static int32_t iso9660_vfs_closedir(int32_t handle) {
    return iso9660_closedir(handle);
}

static bool iso9660_vfs_unlink(const char *path) {
    (void)path;
    return false;
}

static bool iso9660_vfs_close_file(vfs_file_t *file) {
    if (!file || !file->driver_data) {
        return false;
    }
    free(file->driver_data);
    file->driver_data = NULL;
    return true;
}

static const vfs_driver_t g_iso9660_vfs_driver = {
    .fs_type = "iso9660",
    .prefix = NULL,
    .find_file = iso9660_vfs_find_file,
    .read_file = iso9660_vfs_read_file,
    .write_file = iso9660_vfs_write_file,
    .read_at = iso9660_vfs_read_at,
    .write_at = iso9660_vfs_write_at,
    .truncate = iso9660_vfs_truncate,
    .get_file_size = iso9660_vfs_get_file_size,
    .creat = iso9660_vfs_creat,
    .mkdir = iso9660_vfs_mkdir,
    .opendir = iso9660_vfs_opendir,
    .readdir = iso9660_vfs_readdir,
    .closedir = iso9660_vfs_closedir,
    .close_file = iso9660_vfs_close_file,
    .unlink = iso9660_vfs_unlink,
};

const vfs_driver_t *iso9660_vfs_get_driver(void) {
    return &g_iso9660_vfs_driver;
}