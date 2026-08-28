#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "Core/sync/Mutex.h"
#include "kernel/interfaces/vfs_file.h"

struct vfs_driver;

typedef struct vnode {
    uint64_t inode_num;
    uint32_t size;
    uint32_t type;
    const struct vfs_driver *fs;
    void *fs_priv;
    uint32_t refcount;
    mutex_t lock;
} vnode_t;

typedef struct file {
    vnode_t *vnode;
    uint64_t offset;
    uint32_t mode;
    uint32_t refcount;
    vfs_file_t legacy_file;
    mutex_t lock;
} file_t;

typedef struct vfs_dirent {
    char name[256];
    bool is_directory;
    uint32_t size;
} vfs_dirent_t;

/*
 * vfs_media_kind_t -- what a mounted vfs_driver_t is backed by, in terms
 * generic enough that VFS.c never needs to know a specific filesystem's name
 * or driver identity to make a boot-time policy decision (e.g. "prefer the
 * optical-media filesystem as the default root when one is mounted" -- see
 * kernel_main.c's all_fs_initialize() and vfs_set_default_fs_by_kind()
 * below). Every filesystem driver's vfs_driver_t bridge fills this in when
 * it registers with vfs_mount(); it is orthogonal to `fs_type`, which stays
 * around only as a human-readable label and for name-based lookups a caller
 * explicitly asks for (vfs_set_default_fs()), not for VFS-internal policy.
 * See Docs/Others/TODO_OS_Refactor.md 6.1 for the background.
 */
typedef enum {
    VFS_MEDIA_KIND_UNKNOWN = 0,
    VFS_MEDIA_KIND_OPTICAL,  /* read-only optical media (ISO9660, ...) */
    VFS_MEDIA_KIND_DISK,     /* writable disk/removable media (FAT32, exFAT, ...) */
    VFS_MEDIA_KIND_PSEUDO,   /* in-memory pseudo filesystems (devfs, tmpfs, procfs, etcfs) */
} vfs_media_kind_t;

typedef struct vfs_driver {
    const char *fs_type;
    const char *prefix;
    vfs_media_kind_t media_kind;
    bool (*find_file)(const char *path, vfs_file_t *out_file);
    bool (*read_file)(vfs_file_t *file, uint8_t *buffer);
    bool (*write_file)(vfs_file_t *file, const uint8_t *buffer);
    bool (*read_at)(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size);
    bool (*write_at)(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size);
    bool (*truncate)(vfs_file_t *file, uint32_t new_size);
    uint32_t (*get_file_size)(vfs_file_t *file);
    bool (*creat)(const char *path);
    bool (*mkdir)(const char *path);
    int32_t (*opendir)(const char *path);
    int32_t (*readdir)(int32_t handle, vfs_dirent_t *out_entry);
    int32_t (*closedir)(int32_t handle);
    bool (*close_file)(vfs_file_t *file);
    bool (*unlink)(const char *path);
    void (*list_root)(void);
    void (*set_case_sensitive)(bool enabled);
    bool (*get_case_sensitive)(void);
} vfs_driver_t;
