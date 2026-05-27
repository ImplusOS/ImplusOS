#include "VFS.h"
#include <string.h>
#include "Debug/serial/Serial.h"

static vfs_driver_t g_vfs_drivers[16];
static int g_vfs_driver_count = 0;
static const vfs_driver_t *g_default_fs = NULL;

void vfs_mount(const char *prefix, const vfs_driver_t *driver) {
    if (g_vfs_driver_count < 16) {
        g_vfs_drivers[g_vfs_driver_count] = *driver;
        g_vfs_drivers[g_vfs_driver_count].prefix = prefix;
        if (!g_default_fs && strcmp(driver->fs_type, "iso9660") == 0) {
            g_default_fs = &g_vfs_drivers[g_vfs_driver_count];
        }
        g_vfs_driver_count++;
    }
}

bool vfs_set_default_fs(const char *fs_type) {
    for (int i = 0; i < g_vfs_driver_count; i++) {
        if (strcmp(g_vfs_drivers[i].fs_type, fs_type) == 0) {
            g_default_fs = &g_vfs_drivers[i];
            return true;
        }
    }
    return false;
}

static const vfs_driver_t* vfs_find_driver(const char *path) {
    const vfs_driver_t* best_match = g_default_fs;
    size_t best_match_len = 0;

    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (len > 0 && strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            if (len > best_match_len) {
                best_match = &g_vfs_drivers[i];
                best_match_len = len;
            }
        }
    }
    return best_match;
}

bool vfs_init(void) {
    return true;
}

bool vfs_find_file(const char *path, vfs_file_t *out_file) {
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->find_file(path, out_file) : false;
}

bool vfs_read_file(vfs_file_t *file, uint8_t *buffer) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->read_file(file, buffer) : false;
}

bool vfs_write_file(vfs_file_t *file, const uint8_t *buffer) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->write_file(file, buffer) : false;
}

bool vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->read_at(file, offset, buffer, size) : false;
}

bool vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->write_at(file, offset, buffer, size) : false;
}

bool vfs_truncate(vfs_file_t *file, uint32_t new_size) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->truncate(file, new_size) : false;
}

uint32_t vfs_get_file_size(vfs_file_t *file) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->get_file_size(file) : 0;
}

bool vfs_creat(const char *path) {
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->creat(path) : false;
}

bool vfs_mkdir(const char *path) {
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->mkdir(path) : false;
}

int32_t vfs_opendir(const char *path) {
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->opendir(path) : -1;
}

int32_t vfs_readdir(int32_t handle, vfs_dirent_t *out_entry) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->readdir(handle, out_entry) : -1;
}

int32_t vfs_closedir(int32_t handle) {
    const vfs_driver_t *drv = g_default_fs;
    return drv ? drv->closedir(handle) : -1;
}

bool vfs_unlink(const char *path) {
    const vfs_driver_t *drv = vfs_find_driver(path);
    return drv ? drv->unlink(path) : false;
}

void vfs_list_root(void) {
    if (g_default_fs && g_default_fs->list_root) {
        g_default_fs->list_root();
    }
}

void vfs_set_case_sensitive(bool enabled) {
    if (g_default_fs && g_default_fs->set_case_sensitive) {
        g_default_fs->set_case_sensitive(enabled);
    }
}

bool vfs_get_case_sensitive(void) {
    if (g_default_fs && g_default_fs->get_case_sensitive) {
        return g_default_fs->get_case_sensitive();
    }
    return false;
}