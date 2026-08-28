#include "FAT32_VFS_Bridge.h"

#include "Drivers/FileSystem/FAT32/FAT32_Main.h"
#include "Drivers/Module/DriverBinary.h"
#include "Drivers/Module/DriverManager.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ---- driver_manager_find() lookup, lazily (re-)initialized ---- */

static const fat32_driver_t *g_fat32_driver = NULL;
static uint8_t g_fat32_initialized = 0;
static uint8_t g_fat32_init_failed = 0;

static bool fat32_ensure_initialized(void)
{
    const device_t *device = driver_manager_find(DEVICE_TYPE_FILESYSTEM, "FAT32_Driver.ELF");
    const fat32_driver_t *driver = device ? (const fat32_driver_t *)device->ops : NULL;

    if (driver == NULL) {
        g_fat32_driver = NULL;
        g_fat32_initialized = 0;
        return false;
    }

    if (g_fat32_driver != driver) {
        g_fat32_driver = driver;
        g_fat32_initialized = 0;
        g_fat32_init_failed = 0;
    }

    if (g_fat32_init_failed) {
        return false;
    }
    if (g_fat32_initialized) {
        return true;
    }

    if (g_fat32_driver->init == NULL || !g_fat32_driver->init()) {
        g_fat32_init_failed = 1;
        return false;
    }
    g_fat32_initialized = 1;
    return true;
}

bool fat32_init(void)
{
    return fat32_ensure_initialized();
}

/* ---- vfs_file_t <-> FAT32_FILE conversion (former FAT32_VFS_Adapter.c) ---- */

static bool fat32_vfs_find_file(const char *path, vfs_file_t *out_file)
{
    if (!fat32_ensure_initialized()) {
        return false;
    }
    FAT32_FILE *fat_file = (FAT32_FILE *)malloc(sizeof(FAT32_FILE));
    if (!fat_file) {
        return false;
    }

    if (!g_fat32_driver->find_file(path, fat_file)) {
        free(fat_file);
        return false;
    }

    out_file->internal_id = (uint64_t)fat_file;
    out_file->size = fat_file->size;
    out_file->driver_data = fat_file;
    return true;
}

static bool fat32_vfs_read_file(vfs_file_t *file, uint8_t *buffer)
{
    if (!file || !file->driver_data || !fat32_ensure_initialized()) {
        return false;
    }
    return g_fat32_driver->read_file((FAT32_FILE *)file->driver_data, buffer);
}

static bool fat32_vfs_write_file(vfs_file_t *file, const uint8_t *buffer)
{
    if (!file || !file->driver_data || !fat32_ensure_initialized()) {
        return false;
    }
    FAT32_FILE *fat_file = (FAT32_FILE *)file->driver_data;
    bool ok = g_fat32_driver->write_file(fat_file, buffer);
    if (ok) {
        file->size = fat_file->size;
    }
    return ok;
}

static bool fat32_vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!file || !file->driver_data || !fat32_ensure_initialized()) {
        return false;
    }
    return g_fat32_driver->read_at((FAT32_FILE *)file->driver_data, offset, buffer, size);
}

static bool fat32_vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    if (!file || !file->driver_data || !fat32_ensure_initialized()) {
        return false;
    }
    FAT32_FILE *fat_file = (FAT32_FILE *)file->driver_data;
    bool ok = g_fat32_driver->write_at(fat_file, offset, buffer, size);
    if (ok) {
        file->size = fat_file->size;
    }
    return ok;
}

static bool fat32_vfs_truncate(vfs_file_t *file, uint32_t new_size)
{
    if (!file || !file->driver_data || !fat32_ensure_initialized()) {
        return false;
    }
    FAT32_FILE *fat_file = (FAT32_FILE *)file->driver_data;
    bool ok = g_fat32_driver->truncate(fat_file, new_size);
    if (ok) {
        file->size = fat_file->size;
    }
    return ok;
}

static uint32_t fat32_vfs_get_file_size(vfs_file_t *file)
{
    if (!file || !file->driver_data || !fat32_ensure_initialized()) {
        return 0;
    }
    return g_fat32_driver->get_file_size((FAT32_FILE *)file->driver_data);
}

static bool fat32_vfs_creat(const char *path)
{
    if (!fat32_ensure_initialized()) {
        return false;
    }
    return g_fat32_driver->creat(path);
}

static bool fat32_vfs_mkdir(const char *path)
{
    if (!fat32_ensure_initialized()) {
        return false;
    }
    return g_fat32_driver->mkdir(path);
}

static int32_t fat32_vfs_opendir(const char *path)
{
    if (!fat32_ensure_initialized()) {
        return -1;
    }
    return g_fat32_driver->opendir(path);
}

static int32_t fat32_vfs_readdir(int32_t handle, vfs_dirent_t *out_entry)
{
    if (!out_entry || !fat32_ensure_initialized()) {
        return -1;
    }

    FAT32_DIRENT fat_dirent;
    memset(&fat_dirent, 0, sizeof(fat_dirent));
    int32_t result = g_fat32_driver->readdir(handle, &fat_dirent);
    if (result <= 0) {
        return result;
    }

    strncpy(out_entry->name, fat_dirent.name, sizeof(out_entry->name) - 1);
    out_entry->name[sizeof(out_entry->name) - 1] = '\0';
    out_entry->size = fat_dirent.size;
    out_entry->is_directory = (fat_dirent.attributes & 0x10) != 0;

    return result;
}

static int32_t fat32_vfs_closedir(int32_t handle)
{
    if (!fat32_ensure_initialized()) {
        return -1;
    }
    return g_fat32_driver->closedir(handle);
}

static bool fat32_vfs_unlink(const char *path)
{
    if (!fat32_ensure_initialized()) {
        return false;
    }
    return g_fat32_driver->unlink(path);
}

static bool fat32_vfs_close_file(vfs_file_t *file)
{
    if (!file || !file->driver_data) {
        return false;
    }
    free(file->driver_data);
    file->driver_data = NULL;
    return true;
}

static void fat32_vfs_set_case_sensitive(bool enabled)
{
    if (fat32_ensure_initialized() && g_fat32_driver->set_case_sensitive_lookup) {
        g_fat32_driver->set_case_sensitive_lookup(enabled);
    }
}

static bool fat32_vfs_get_case_sensitive(void)
{
    if (fat32_ensure_initialized() && g_fat32_driver->get_case_sensitive_lookup) {
        return g_fat32_driver->get_case_sensitive_lookup();
    }
    return false;
}

static void fat32_vfs_list_root(void)
{
    if (fat32_ensure_initialized() && g_fat32_driver->list_root_files) {
        g_fat32_driver->list_root_files();
    }
}

static const vfs_driver_t g_fat32_vfs_driver = {
    .fs_type = "fat32",
    .prefix = NULL,
    .media_kind = VFS_MEDIA_KIND_DISK,
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
    .list_root = fat32_vfs_list_root,
    .set_case_sensitive = fat32_vfs_set_case_sensitive,
    .get_case_sensitive = fat32_vfs_get_case_sensitive,
};

const vfs_driver_t *fat32_vfs_get_driver(void)
{
    return &g_fat32_vfs_driver;
}
