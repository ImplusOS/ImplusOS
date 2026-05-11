
#include "VFS.h"
#include <string.h>
#include "Debug/serial/Serial.h"

static vfs_driver_t g_vfs_drivers[16];
static int g_vfs_driver_count = 0;

void vfs_mount(const char *prefix, const vfs_driver_t *driver) {
    if (g_vfs_driver_count < 16) {
        g_vfs_drivers[g_vfs_driver_count] = *driver;
        g_vfs_drivers[g_vfs_driver_count].prefix = prefix;
        g_vfs_driver_count++;
    }
}

static const vfs_driver_t* vfs_find_driver(const char *path) {
    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            return &g_vfs_drivers[i];
        }
    }
    return NULL;
}

bool vfs_init(void)
{
    if (!driver_manager_fs_init(NULL)) return false;
    
    static vfs_driver_t fat32_vfs;
    fat32_vfs.find_file = driver_manager_fs_find_file;
    fat32_vfs.read_file = driver_manager_fs_read_file;
    fat32_vfs.write_file = driver_manager_fs_write_file;
    fat32_vfs.read_at = driver_manager_fs_read_at;
    fat32_vfs.write_at = driver_manager_fs_write_at;
    fat32_vfs.truncate = driver_manager_fs_truncate;
    fat32_vfs.get_file_size = driver_manager_fs_get_file_size;
    fat32_vfs.creat = driver_manager_fs_creat;
    fat32_vfs.mkdir = driver_manager_fs_mkdir;
    fat32_vfs.opendir = driver_manager_fs_opendir;
    fat32_vfs.readdir = driver_manager_fs_readdir;
    fat32_vfs.closedir = driver_manager_fs_closedir;
    fat32_vfs.unlink = driver_manager_fs_unlink;
    
    vfs_mount("/", &fat32_vfs);
    return true;
}

bool vfs_find_file(const char *path, VFS_FILE *out_file)
{
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->find_file(path, out_file) : false;
}

bool vfs_read_file(VFS_FILE *file, uint8_t *buffer)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->read_file(file, buffer) : false;
}

bool vfs_write_file(VFS_FILE *file, const uint8_t *buffer)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->write_file(file, buffer) : false;
}

bool vfs_read_at(VFS_FILE *file, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->read_at(file, offset, buffer, size) : false;
}

bool vfs_write_at(VFS_FILE *file, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->write_at(file, offset, buffer, size) : false;
}

bool vfs_truncate(VFS_FILE *file, uint32_t new_size)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->truncate(file, new_size) : false;
}

uint32_t vfs_get_file_size(VFS_FILE *file)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->get_file_size(file) : 0;
}

void vfs_list_root(void)
{
    driver_manager_fs_list_root_files();
}

bool vfs_creat(const char *path)
{
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->creat(path) : false;
}

bool vfs_mkdir(const char *path)
{
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->mkdir(path) : false;
}

int32_t vfs_opendir(const char *path)
{
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->opendir(path) : -1;
}

int32_t vfs_readdir(int32_t handle, VFS_DIRENT *out_entry)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->readdir(handle, out_entry) : -1;
}

int32_t vfs_closedir(int32_t handle)
{
    const vfs_driver_t *drv = vfs_find_driver("/");
    return drv ? drv->closedir(handle) : -1;
}

bool vfs_unlink(const char *path)
{
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->unlink(path) : false;
}

void vfs_set_case_sensitive(bool enabled)
{
    driver_manager_fs_set_case_sensitive_lookup(enabled);
}

bool vfs_get_case_sensitive(void)
{
    return driver_manager_fs_get_case_sensitive_lookup();
}
