#include "VFS.h"
#include <string.h>
#include "Core/sync/Spinlock.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"

static vfs_driver_t g_vfs_drivers[16];
static int g_vfs_driver_count = 0;
static const vfs_driver_t *g_default_fs = NULL;

typedef struct {
    const vfs_driver_t *drv;
    int32_t driver_handle;
} vfs_directory_handle_t;

static vfs_directory_handle_t g_vfs_directory_handles[32];
static spinlock_t g_vfs_lock;

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

bool vfs_init(void) {
    spinlock_init(&g_vfs_lock);
    memset(g_vfs_directory_handles, 0, sizeof(g_vfs_directory_handles));
    return true;
}

static bool vfs_probe_file_on_driver(const vfs_driver_t *driver,
                                     const char *path,
                                     uint32_t min_size,
                                     const uint8_t *magic,
                                     uint32_t magic_size)
{
    if (driver == NULL || driver->find_file == NULL || path == NULL) {
        return false;
    }
    if (magic_size > 16u) {
        return false;
    }

    vfs_file_t file;
    memset(&file, 0, sizeof(file));
    if (!driver->find_file(path, &file)) {
        return false;
    }
    file.fs_driver = driver;

    bool ok = file.size >= min_size;
    if (ok && magic != NULL && magic_size != 0u) {
        uint8_t probe[16];
        memset(probe, 0, sizeof(probe));
        ok = driver->read_at != NULL &&
             driver->read_at(&file, 0u, probe, magic_size) &&
             memcmp(probe, magic, magic_size) == 0;
    }

    if (driver->close_file != NULL) {
        (void)driver->close_file(&file);
    }
    return ok;
}

bool vfs_set_default_fs_for_file(const char *path,
                                 uint32_t min_size,
                                 const uint8_t *magic,
                                 uint32_t magic_size)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (g_default_fs != NULL) {
        if (vfs_probe_file_on_driver(g_default_fs, path, min_size,
                                     magic, magic_size)) {
            return true;
        }
    }

    for (int i = 0; i < g_vfs_driver_count; i++) {
        const vfs_driver_t *driver = &g_vfs_drivers[i];
        if (driver == g_default_fs) {
            continue;
        }
        if (vfs_probe_file_on_driver(driver, path, min_size,
                                     magic, magic_size)) {
            g_default_fs = driver;
            return true;
        }
    }

    return false;
}

bool vfs_find_file(const char *path, vfs_file_t *out_file) {
    size_t best_match_len = 0;

    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (len > 0 && strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            if (len > best_match_len) {
                best_match_len = len;
            }
        }
    }

    if (best_match_len > 0) {
        for (int i = 0; i < g_vfs_driver_count; i++) {
            if (strlen(g_vfs_drivers[i].prefix) == best_match_len &&
                strncmp(path, g_vfs_drivers[i].prefix, best_match_len) == 0) {
                if (g_vfs_drivers[i].find_file(path, out_file)) {
                    out_file->fs_driver = &g_vfs_drivers[i];
                    return true;
                }
            }
        }
        return false;
    }

    if (g_default_fs) {
        if (g_default_fs->find_file(path, out_file)) {
            out_file->fs_driver = g_default_fs;
            return true;
        }
    }

    for (int i = 0; i < g_vfs_driver_count; i++) {
        if (&g_vfs_drivers[i] == g_default_fs) continue;
        if (g_vfs_drivers[i].prefix[0] == '\0') {
            if (g_vfs_drivers[i].find_file(path, out_file)) {
                out_file->fs_driver = &g_vfs_drivers[i];
                return true;
            }
        }
    }

    return false;
}

bool vfs_read_file(vfs_file_t *file, uint8_t *buffer) {
    if (!file || !file->fs_driver) return false;
    return file->fs_driver->read_file(file, buffer);
}

bool vfs_write_file(vfs_file_t *file, const uint8_t *buffer) {
    if (!file || !file->fs_driver) return false;
    return file->fs_driver->write_file(file, buffer);
}

bool vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size) {
    if (!file || !file->fs_driver) return false;
    bool (*read_at)(vfs_file_t *, uint32_t, uint8_t *, uint32_t) = file->fs_driver->read_at;
    if (!read_at) {
        return false;
    }
    return read_at(file, offset, buffer, size);
}

bool vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size) {
    if (!file || !file->fs_driver) return false;
    return file->fs_driver->write_at(file, offset, buffer, size);
}

bool vfs_truncate(vfs_file_t *file, uint32_t new_size) {
    if (!file || !file->fs_driver) return false;
    return file->fs_driver->truncate(file, new_size);
}

uint32_t vfs_get_file_size(vfs_file_t *file) {
    if (!file || !file->fs_driver) return 0;
    return file->fs_driver->get_file_size(file);
}

bool vfs_close_file(vfs_file_t *file) {
    if (!file || !file->fs_driver || !file->fs_driver->close_file) {
        return false;
    }
    bool result = file->fs_driver->close_file(file);
    file->driver_data = NULL;
    file->internal_id = 0;
    file->size = 0;
    file->fs_driver = NULL;
    return result;
}

bool vfs_creat(const char *path) {
    size_t best_match_len = 0;
    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (len > 0 && strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            if (len > best_match_len) best_match_len = len;
        }
    }

    if (best_match_len > 0) {
        for (int i = 0; i < g_vfs_driver_count; i++) {
            if (strlen(g_vfs_drivers[i].prefix) == best_match_len &&
                strncmp(path, g_vfs_drivers[i].prefix, best_match_len) == 0) {
                if (g_vfs_drivers[i].creat(path)) return true;
            }
        }
        return false;
    }

    if (g_default_fs && g_default_fs->creat(path)) return true;

    for (int i = 0; i < g_vfs_driver_count; i++) {
        if (&g_vfs_drivers[i] == g_default_fs) continue;
        if (g_vfs_drivers[i].prefix[0] == '\0') {
            if (g_vfs_drivers[i].creat(path)) return true;
        }
    }
    return false;
}

bool vfs_mkdir(const char *path) {
    size_t best_match_len = 0;
    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (len > 0 && strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            if (len > best_match_len) best_match_len = len;
        }
    }

    if (best_match_len > 0) {
        for (int i = 0; i < g_vfs_driver_count; i++) {
            if (strlen(g_vfs_drivers[i].prefix) == best_match_len &&
                strncmp(path, g_vfs_drivers[i].prefix, best_match_len) == 0) {
                if (g_vfs_drivers[i].mkdir(path)) return true;
            }
        }
        return false;
    }

    if (g_default_fs && g_default_fs->mkdir(path)) return true;

    for (int i = 0; i < g_vfs_driver_count; i++) {
        if (&g_vfs_drivers[i] == g_default_fs) continue;
        if (g_vfs_drivers[i].prefix[0] == '\0') {
            if (g_vfs_drivers[i].mkdir(path)) return true;
        }
    }
    return false;
}

int32_t vfs_opendir(const char *path) {
    const vfs_driver_t *drv = NULL;
    int32_t handle = -1;

    size_t best_match_len = 0;
    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (len > 0 && strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            if (len > best_match_len) best_match_len = len;
        }
    }

    if (best_match_len > 0) {
        for (int i = 0; i < g_vfs_driver_count; i++) {
            if (strlen(g_vfs_drivers[i].prefix) == best_match_len &&
                strncmp(path, g_vfs_drivers[i].prefix, best_match_len) == 0) {
                handle = g_vfs_drivers[i].opendir(path);
                if (handle >= 0) {
                    drv = &g_vfs_drivers[i];
                    break;
                }
            }
        }
    } else {
        if (g_default_fs) {
            handle = g_default_fs->opendir(path);
            if (handle >= 0) {
                drv = g_default_fs;
            }
        }

        if (handle < 0) {
            for (int i = 0; i < g_vfs_driver_count; i++) {
                if (&g_vfs_drivers[i] == g_default_fs) continue;
                if (g_vfs_drivers[i].prefix[0] == '\0') {
                    handle = g_vfs_drivers[i].opendir(path);
                    if (handle >= 0) {
                        drv = &g_vfs_drivers[i];
                        break;
                    }
                }
            }
        }
    }

    if (handle >= 0 && drv) {
        spinlock_lock(&g_vfs_lock);
        for (int i = 0; i < 32; i++) {
            if (g_vfs_directory_handles[i].drv == NULL) {
                g_vfs_directory_handles[i].drv = drv;
                g_vfs_directory_handles[i].driver_handle = handle;
                spinlock_unlock(&g_vfs_lock);
                return i;
            }
        }
        spinlock_unlock(&g_vfs_lock);
        drv->closedir(handle);
    }
    return -1;
}

int32_t vfs_readdir(int32_t handle, vfs_dirent_t *out_entry) {
    if (handle < 0 || handle >= 32) return -1;
    spinlock_lock(&g_vfs_lock);
    vfs_directory_handle_t *h = &g_vfs_directory_handles[handle];
    if (!h->drv) {
        spinlock_unlock(&g_vfs_lock);
        return -1;
    }
    const vfs_driver_t *drv = h->drv;
    int32_t driver_handle = h->driver_handle;
    spinlock_unlock(&g_vfs_lock);

    return drv->readdir(driver_handle, out_entry);
}

int32_t vfs_closedir(int32_t handle) {
    if (handle < 0 || handle >= 32) return -1;
    spinlock_lock(&g_vfs_lock);
    vfs_directory_handle_t *h = &g_vfs_directory_handles[handle];
    if (!h->drv) {
        spinlock_unlock(&g_vfs_lock);
        return -1;
    }
    const vfs_driver_t *drv = h->drv;
    int32_t driver_handle = h->driver_handle;
    h->drv = NULL;
    h->driver_handle = 0;
    spinlock_unlock(&g_vfs_lock);

    return drv->closedir(driver_handle);
}

bool vfs_unlink(const char *path) {
    size_t best_match_len = 0;
    for (int i = 0; i < g_vfs_driver_count; i++) {
        size_t len = strlen(g_vfs_drivers[i].prefix);
        if (len > 0 && strncmp(path, g_vfs_drivers[i].prefix, len) == 0) {
            if (len > best_match_len) best_match_len = len;
        }
    }

    if (best_match_len > 0) {
        for (int i = 0; i < g_vfs_driver_count; i++) {
            if (strlen(g_vfs_drivers[i].prefix) == best_match_len &&
                strncmp(path, g_vfs_drivers[i].prefix, best_match_len) == 0) {
                if (g_vfs_drivers[i].unlink(path)) return true;
            }
        }
        return false;
    }

    if (g_default_fs && g_default_fs->unlink(path)) return true;

    for (int i = 0; i < g_vfs_driver_count; i++) {
        if (&g_vfs_drivers[i] == g_default_fs) continue;
        if (g_vfs_drivers[i].prefix[0] == '\0') {
            if (g_vfs_drivers[i].unlink(path)) return true;
        }
    }
    return false;
}

bool vfs_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path || old_path[0] == '\0' ||
        new_path[0] == '\0') return false;
    if (strcmp(old_path, new_path) == 0) return true;

    vfs_file_t source;
    if (!vfs_find_file(old_path, &source)) return false;
    uint32_t size = vfs_get_file_size(&source);

    vfs_file_t existing;
    if (vfs_find_file(new_path, &existing)) {
        if (existing.fs_driver == source.fs_driver &&
            existing.internal_id == source.internal_id) {
            (void)vfs_close_file(&existing);
            (void)vfs_close_file(&source);
            return true;
        }
        (void)vfs_close_file(&existing);
        if (!vfs_unlink(new_path)) {
            (void)vfs_close_file(&source);
            return false;
        }
    }
    if (!vfs_creat(new_path)) {
        (void)vfs_close_file(&source);
        return false;
    }

    vfs_file_t destination;
    if (!vfs_find_file(new_path, &destination)) {
        (void)vfs_close_file(&source);
        (void)vfs_unlink(new_path);
        return false;
    }

    uint8_t buffer[4096];
    bool ok = true;
    for (uint32_t offset = 0; offset < size;) {
        uint32_t chunk = size - offset;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (!vfs_read_at(&source, offset, buffer, chunk) ||
            !vfs_write_at(&destination, offset, buffer, chunk)) {
            ok = false;
            break;
        }
        offset += chunk;
    }
    if (ok) ok = vfs_truncate(&destination, size);
    (void)vfs_close_file(&source);
    (void)vfs_close_file(&destination);
    if (!ok) {
        (void)vfs_unlink(new_path);
        return false;
    }
    if (!vfs_unlink(old_path)) {
        (void)vfs_unlink(new_path);
        return false;
    }
    return true;
}

int vfs_driver_count_get(void) {
    return g_vfs_driver_count;
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
