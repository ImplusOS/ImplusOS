#include "FS_VFS_Bridge.h"

#include "Drivers/Module/DriverBinary.h"
#include "Drivers/Module/DriverManager.h"
#include "kernel/interfaces/fs_module_ops.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct {
    const char            *elf_name;
    const fs_module_ops_t *ops;         /* resolved lazily via driver_manager_find() */
    bool                   initialized;
    bool                   init_failed;
    vfs_driver_t           vfs;         /* handed to vfs_mount(); hooks are this file's thunks */
} fs_bridge_state_t;

static fs_bridge_state_t g_fs_bridges[FS_BRIDGE_COUNT] = {
    [FS_BRIDGE_FAT32]   = { .elf_name = "FAT32_Driver.ELF" },
    [FS_BRIDGE_EXFAT]   = { .elf_name = "exFAT_Driver.ELF" },
    [FS_BRIDGE_ISO9660] = { .elf_name = "ISO9660_Driver.ELF" },
};

/* Per-open wrapper stashed in vfs_file_t.driver_data so the handle-based
 * hooks (which only get a vfs_file_t) can recover both the owning
 * filesystem and its opaque file handle. */
typedef struct {
    fs_bridge_state_t *state;
    void              *handle;   /* fs-private, ops->handle_size bytes */
} fs_open_t;

/* ---- lazy resolve + init-once, formerly FAT32_VFS_Bridge.c's pattern,
 * now used for every filesystem ---- */
static bool fs_bridge_ensure(fs_bridge_state_t *state)
{
    const device_t *device =
        driver_manager_find(DEVICE_TYPE_FILESYSTEM, state->elf_name);
    const fs_module_ops_t *ops =
        device ? (const fs_module_ops_t *)device->ops : NULL;

    if (ops == NULL) {
        state->ops = NULL;
        state->initialized = false;
        return false;
    }

    if (state->ops != ops) {          /* module (re)loaded -- start over */
        state->ops = ops;
        state->initialized = false;
        state->init_failed = false;
    }

    if (state->init_failed) {
        return false;
    }
    if (state->initialized) {
        return true;
    }
    if (ops->init == NULL || !ops->init()) {
        state->init_failed = true;
        return false;
    }
    state->initialized = true;
    return true;
}

/* ---- handle-based hooks: recover state from vfs_file_t.driver_data ---- */

static fs_open_t *fs_open_of(vfs_file_t *file)
{
    return (file && file->driver_data) ? (fs_open_t *)file->driver_data : NULL;
}

static void fs_open_refresh_size(fs_open_t *open, vfs_file_t *file)
{
    if (open->state->ops->get_file_size) {
        file->size = open->state->ops->get_file_size(open->handle);
    }
}

static bool thunk_read_file(vfs_file_t *file, uint8_t *buffer)
{
    fs_open_t *open = fs_open_of(file);
    if (!open || !fs_bridge_ensure(open->state) || !open->state->ops->read_file) {
        return false;
    }
    return open->state->ops->read_file(open->handle, buffer);
}

static bool thunk_write_file(vfs_file_t *file, const uint8_t *buffer)
{
    fs_open_t *open = fs_open_of(file);
    if (!open || !fs_bridge_ensure(open->state) || !open->state->ops->write_file) {
        return false;
    }
    bool ok = open->state->ops->write_file(open->handle, buffer);
    if (ok) {
        fs_open_refresh_size(open, file);
    }
    return ok;
}

static bool thunk_read_at(vfs_file_t *file, uint32_t offset,
                          uint8_t *buffer, uint32_t size)
{
    fs_open_t *open = fs_open_of(file);
    if (!open || !fs_bridge_ensure(open->state) || !open->state->ops->read_at) {
        return false;
    }
    return open->state->ops->read_at(open->handle, offset, buffer, size);
}

static bool thunk_write_at(vfs_file_t *file, uint32_t offset,
                           const uint8_t *buffer, uint32_t size)
{
    fs_open_t *open = fs_open_of(file);
    if (!open || !fs_bridge_ensure(open->state) || !open->state->ops->write_at) {
        return false;
    }
    bool ok = open->state->ops->write_at(open->handle, offset, buffer, size);
    if (ok) {
        fs_open_refresh_size(open, file);
    }
    return ok;
}

static bool thunk_truncate(vfs_file_t *file, uint32_t new_size)
{
    fs_open_t *open = fs_open_of(file);
    if (!open || !fs_bridge_ensure(open->state) || !open->state->ops->truncate) {
        return false;
    }
    bool ok = open->state->ops->truncate(open->handle, new_size);
    if (ok) {
        fs_open_refresh_size(open, file);
    }
    return ok;
}

static uint32_t thunk_get_file_size(vfs_file_t *file)
{
    fs_open_t *open = fs_open_of(file);
    if (!open || !fs_bridge_ensure(open->state) || !open->state->ops->get_file_size) {
        return 0;
    }
    return open->state->ops->get_file_size(open->handle);
}

static bool thunk_close_file(vfs_file_t *file)
{
    fs_open_t *open = fs_open_of(file);
    if (!open) {
        return false;
    }
    if (open->state->ops && open->state->ops->release) {
        open->state->ops->release(open->handle);
    }
    free(open->handle);
    free(open);
    file->driver_data = NULL;
    return true;
}

/* ---- per-filesystem hooks: need to know which filesystem ---- */

static bool bridge_find_file(fs_bridge_state_t *state,
                             const char *path, vfs_file_t *out_file)
{
    if (!fs_bridge_ensure(state) || !state->ops->find_file ||
        state->ops->handle_size == 0u) {
        return false;
    }

    fs_open_t *open = (fs_open_t *)malloc(sizeof(*open));
    if (!open) {
        return false;
    }
    open->state = state;
    open->handle = malloc(state->ops->handle_size);
    if (!open->handle) {
        free(open);
        return false;
    }

    uint64_t internal_id = 0;
    uint32_t size = 0;
    if (!state->ops->find_file(path, open->handle, &internal_id, &size)) {
        free(open->handle);
        free(open);
        return false;
    }

    out_file->internal_id = internal_id;
    out_file->size = size;
    out_file->driver_data = open;
    return true;
}

static bool bridge_creat(fs_bridge_state_t *state, const char *path)
{
    return fs_bridge_ensure(state) && state->ops->creat && state->ops->creat(path);
}

static bool bridge_mkdir(fs_bridge_state_t *state, const char *path)
{
    return fs_bridge_ensure(state) && state->ops->mkdir && state->ops->mkdir(path);
}

static bool bridge_unlink(fs_bridge_state_t *state, const char *path)
{
    return fs_bridge_ensure(state) && state->ops->unlink && state->ops->unlink(path);
}

static int32_t bridge_opendir(fs_bridge_state_t *state, const char *path)
{
    return (fs_bridge_ensure(state) && state->ops->opendir)
               ? state->ops->opendir(path) : -1;
}

static int32_t bridge_readdir(fs_bridge_state_t *state, int32_t handle,
                              vfs_dirent_t *out_entry)
{
    return (fs_bridge_ensure(state) && state->ops->readdir)
               ? state->ops->readdir(handle, out_entry) : -1;
}

static int32_t bridge_closedir(fs_bridge_state_t *state, int32_t handle)
{
    return (fs_bridge_ensure(state) && state->ops->closedir)
               ? state->ops->closedir(handle) : -1;
}

static void bridge_list_root(fs_bridge_state_t *state)
{
    if (fs_bridge_ensure(state) && state->ops->list_root) {
        state->ops->list_root();
    }
}

static void bridge_set_case_sensitive(fs_bridge_state_t *state, bool enabled)
{
    if (fs_bridge_ensure(state) && state->ops->set_case_sensitive) {
        state->ops->set_case_sensitive(enabled);
    }
}

static bool bridge_get_case_sensitive(fs_bridge_state_t *state)
{
    return (fs_bridge_ensure(state) && state->ops->get_case_sensitive)
               ? state->ops->get_case_sensitive() : false;
}

/*
 * One set of per-entry thunks per filesystem. They exist only to bind the
 * fixed vfs_driver_t hook signatures (which carry no filesystem context for
 * path/dir/misc operations) to a specific g_fs_bridges[] row; all real work
 * is in the bridge_* / thunk_* helpers above.
 */
#define FS_BRIDGE_DEFINE_ENTRY(tag, id)                                             \
    static bool    tag##_find_file(const char *p, vfs_file_t *f)                     \
    { return bridge_find_file(&g_fs_bridges[id], p, f); }                            \
    static bool    tag##_creat(const char *p)                                       \
    { return bridge_creat(&g_fs_bridges[id], p); }                                  \
    static bool    tag##_mkdir(const char *p)                                       \
    { return bridge_mkdir(&g_fs_bridges[id], p); }                                  \
    static bool    tag##_unlink(const char *p)                                      \
    { return bridge_unlink(&g_fs_bridges[id], p); }                                 \
    static int32_t tag##_opendir(const char *p)                                     \
    { return bridge_opendir(&g_fs_bridges[id], p); }                                \
    static int32_t tag##_readdir(int32_t h, vfs_dirent_t *e)                        \
    { return bridge_readdir(&g_fs_bridges[id], h, e); }                             \
    static int32_t tag##_closedir(int32_t h)                                        \
    { return bridge_closedir(&g_fs_bridges[id], h); }                               \
    static void    tag##_list_root(void)                                            \
    { bridge_list_root(&g_fs_bridges[id]); }                                        \
    static void    tag##_set_case_sensitive(bool e)                                 \
    { bridge_set_case_sensitive(&g_fs_bridges[id], e); }                            \
    static bool    tag##_get_case_sensitive(void)                                   \
    { return bridge_get_case_sensitive(&g_fs_bridges[id]); }                        \
    static void    tag##_install_vfs(void)                                          \
    {                                                                              \
        fs_bridge_state_t *s = &g_fs_bridges[id];                                   \
        s->vfs.fs_type    = s->ops ? s->ops->fs_type : #tag;                        \
        s->vfs.prefix     = NULL;                                                   \
        s->vfs.media_kind = s->ops ? s->ops->media_kind : VFS_MEDIA_KIND_UNKNOWN;   \
        s->vfs.find_file      = tag##_find_file;                                    \
        s->vfs.read_file      = thunk_read_file;                                    \
        s->vfs.write_file     = thunk_write_file;                                   \
        s->vfs.read_at        = thunk_read_at;                                      \
        s->vfs.write_at       = thunk_write_at;                                     \
        s->vfs.truncate       = thunk_truncate;                                     \
        s->vfs.get_file_size  = thunk_get_file_size;                                \
        s->vfs.creat          = tag##_creat;                                        \
        s->vfs.mkdir          = tag##_mkdir;                                        \
        s->vfs.opendir        = tag##_opendir;                                      \
        s->vfs.readdir        = tag##_readdir;                                      \
        s->vfs.closedir       = tag##_closedir;                                     \
        s->vfs.close_file     = thunk_close_file;                                   \
        s->vfs.unlink         = tag##_unlink;                                       \
        s->vfs.list_root      = tag##_list_root;                                    \
        s->vfs.set_case_sensitive = tag##_set_case_sensitive;                       \
        s->vfs.get_case_sensitive = tag##_get_case_sensitive;                       \
    }

FS_BRIDGE_DEFINE_ENTRY(fat32,   FS_BRIDGE_FAT32)
FS_BRIDGE_DEFINE_ENTRY(exfat,   FS_BRIDGE_EXFAT)
FS_BRIDGE_DEFINE_ENTRY(iso9660, FS_BRIDGE_ISO9660)

static void fs_bridge_install_vfs(fs_bridge_id_t id)
{
    switch (id) {
    case FS_BRIDGE_FAT32:   fat32_install_vfs();   break;
    case FS_BRIDGE_EXFAT:   exfat_install_vfs();   break;
    case FS_BRIDGE_ISO9660: iso9660_install_vfs(); break;
    default:                                       break;
    }
}

bool fs_bridge_init(fs_bridge_id_t id)
{
    if (id >= FS_BRIDGE_COUNT) {
        return false;
    }
    bool ok = fs_bridge_ensure(&g_fs_bridges[id]);
    fs_bridge_install_vfs(id);   /* pick up fs_type/media_kind from the resolved ops */
    return ok;
}

const vfs_driver_t *fs_bridge_vfs_driver(fs_bridge_id_t id)
{
    if (id >= FS_BRIDGE_COUNT) {
        return NULL;
    }
    (void)fs_bridge_ensure(&g_fs_bridges[id]);
    fs_bridge_install_vfs(id);
    return &g_fs_bridges[id].vfs;
}
