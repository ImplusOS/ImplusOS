#include "exFAT_VFS_Bridge.h"

#include "Drivers/FileSystem/exFAT/exFAT_Main.h"
#include "Drivers/Module/DriverBinary.h"
#include "Drivers/Module/DriverManager.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Same lazy driver_manager_find() + init-once pattern as FAT32_VFS_Bridge.c
 * / ISO9660_VFS_Bridge.c. exFAT is read-only for now (see exFAT_Main.h) so
 * this bridge, like ISO9660's, reports failure for every write-side
 * vfs_driver_t hook rather than forwarding to a driver_api that doesn't
 * have them. */

static const exfat_driver_t *g_exfat_driver = NULL;
static uint8_t g_exfat_initialized = 0;
static uint8_t g_exfat_init_failed = 0;

static bool exfat_ensure_initialized(void)
{
    const device_t *device = driver_manager_find(DEVICE_TYPE_FILESYSTEM, "exFAT_Driver.ELF");
    const exfat_driver_t *driver = device ? (const exfat_driver_t *)device->ops : NULL;

    if (driver == NULL) {
        g_exfat_driver = NULL;
        g_exfat_initialized = 0;
        return false;
    }

    if (g_exfat_driver != driver) {
        g_exfat_driver = driver;
        g_exfat_initialized = 0;
        g_exfat_init_failed = 0;
    }

    if (g_exfat_init_failed) {
        return false;
    }
    if (g_exfat_initialized) {
        return true;
    }

    if (g_exfat_driver->init == NULL || !g_exfat_driver->init()) {
        g_exfat_init_failed = 1;
        return false;
    }
    g_exfat_initialized = 1;
    return true;
}

bool exfat_init(void)
{
    return exfat_ensure_initialized();
}

/* ---- vfs_file_t <-> exFAT_FILE conversion ----
 * exFAT_FILE.size is 64-bit (the on-disk format allows files > 4GiB); the
 * VFS layer's vfs_file_t.size and get_file_size() are both 32-bit (see
 * Kernel/include/kernel/interfaces/vfs_file.h -- FAT32 shares this same
 * limit already), so sizes are clamped to UINT32_MAX here at the bridge
 * boundary. read_at()/read_file() themselves are unaffected: exfat_read_at()
 * takes a 32-bit offset/size, same as every other VFS-facing driver. */

static uint32_t exfat_clamp_size(uint64_t size)
{
    return (size > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)size;
}

static bool exfat_vfs_find_file(const char *path, vfs_file_t *out_file)
{
    if (!exfat_ensure_initialized()) {
        return false;
    }
    exFAT_FILE *exfat_file = (exFAT_FILE *)malloc(sizeof(exFAT_FILE));
    if (!exfat_file) {
        return false;
    }

    if (!g_exfat_driver->find_file(path, exfat_file)) {
        free(exfat_file);
        return false;
    }

    out_file->internal_id = (uint64_t)exfat_file;
    out_file->size = exfat_clamp_size(exfat_file->size);
    out_file->driver_data = exfat_file;
    return true;
}

static bool exfat_vfs_read_file(vfs_file_t *file, uint8_t *buffer)
{
    if (!file || !file->driver_data || !exfat_ensure_initialized()) {
        return false;
    }
    return g_exfat_driver->read_file((exFAT_FILE *)file->driver_data, buffer);
}

static bool exfat_vfs_write_file(vfs_file_t *file, const uint8_t *buffer)
{
    (void)file;
    (void)buffer;
    return false;
}

static bool exfat_vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!file || !file->driver_data || !exfat_ensure_initialized()) {
        return false;
    }
    return g_exfat_driver->read_at((exFAT_FILE *)file->driver_data, offset, buffer, size);
}

static bool exfat_vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    (void)file;
    (void)offset;
    (void)buffer;
    (void)size;
    return false;
}

static bool exfat_vfs_truncate(vfs_file_t *file, uint32_t new_size)
{
    (void)file;
    (void)new_size;
    return false;
}

static uint32_t exfat_vfs_get_file_size(vfs_file_t *file)
{
    if (!file || !file->driver_data || !exfat_ensure_initialized()) {
        return 0;
    }
    return exfat_clamp_size(g_exfat_driver->get_file_size((exFAT_FILE *)file->driver_data));
}

static bool exfat_vfs_creat(const char *path)
{
    (void)path;
    return false;
}

static bool exfat_vfs_mkdir(const char *path)
{
    (void)path;
    return false;
}

static int32_t exfat_vfs_opendir(const char *path)
{
    if (!exfat_ensure_initialized()) {
        return -1;
    }
    return g_exfat_driver->opendir(path);
}

static int32_t exfat_vfs_readdir(int32_t handle, vfs_dirent_t *out_entry)
{
    if (!out_entry || !exfat_ensure_initialized()) {
        return -1;
    }

    exFAT_DIRENT exfat_dirent;
    memset(&exfat_dirent, 0, sizeof(exfat_dirent));
    int32_t result = g_exfat_driver->readdir(handle, &exfat_dirent);
    if (result <= 0) {
        return result;
    }

    strncpy(out_entry->name, exfat_dirent.name, sizeof(out_entry->name) - 1);
    out_entry->name[sizeof(out_entry->name) - 1] = '\0';
    out_entry->size = exfat_clamp_size(exfat_dirent.size);
    out_entry->is_directory = exfat_dirent.is_directory != 0;

    return result;
}

static int32_t exfat_vfs_closedir(int32_t handle)
{
    if (!exfat_ensure_initialized()) {
        return -1;
    }
    return g_exfat_driver->closedir(handle);
}

static bool exfat_vfs_unlink(const char *path)
{
    (void)path;
    return false;
}

static bool exfat_vfs_close_file(vfs_file_t *file)
{
    if (!file || !file->driver_data) {
        return false;
    }
    free(file->driver_data);
    file->driver_data = NULL;
    return true;
}

static void exfat_vfs_list_root(void)
{
    if (exfat_ensure_initialized() && g_exfat_driver->list_root_files) {
        g_exfat_driver->list_root_files();
    }
}

static const vfs_driver_t g_exfat_vfs_driver = {
    .fs_type = "exfat",
    .prefix = NULL,
    .media_kind = VFS_MEDIA_KIND_DISK,
    .find_file = exfat_vfs_find_file,
    .read_file = exfat_vfs_read_file,
    .write_file = exfat_vfs_write_file,
    .read_at = exfat_vfs_read_at,
    .write_at = exfat_vfs_write_at,
    .truncate = exfat_vfs_truncate,
    .get_file_size = exfat_vfs_get_file_size,
    .creat = exfat_vfs_creat,
    .mkdir = exfat_vfs_mkdir,
    .opendir = exfat_vfs_opendir,
    .readdir = exfat_vfs_readdir,
    .closedir = exfat_vfs_closedir,
    .close_file = exfat_vfs_close_file,
    .unlink = exfat_vfs_unlink,
    .list_root = exfat_vfs_list_root,
};

const vfs_driver_t *exfat_vfs_get_driver(void)
{
    return &g_exfat_vfs_driver;
}
