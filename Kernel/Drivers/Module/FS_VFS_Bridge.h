#pragma once

/*
 * FS_VFS_Bridge -- the single kernel-resident glue between the loadable
 * filesystem driver modules (FAT32_Driver.ELF, exFAT_Driver.ELF,
 * ISO9660_Driver.ELF) and Core/vfs/VFS.c, replacing the three
 * near-identical FAT32_VFS_Bridge.c / exFAT_VFS_Bridge.c /
 * ISO9660_VFS_Bridge.c files. Each module now exports one common
 * fs_module_ops_t (see kernel/interfaces/fs_module_ops.h) instead of its
 * own bespoke vtable, and this file is the only place that knows how to
 * turn that into the vfs_driver_t vfs_mount() wants -- allocating the
 * opaque per-open file handle, converting write results back into
 * vfs_file_t.size, and lazily (re-)resolving + init'ing the module the way
 * FAT32_VFS_Bridge.c used to (that pattern is now used for every fs).
 *
 * Adding a filesystem: give its module an fs_module_ops_t, then add one
 * row to g_fs_bridges[] in FS_VFS_Bridge.c and one fs_bridge_id_t here. No
 * new bridge file.
 */

#include <stdbool.h>

#include "kernel/interfaces/vfs_types.h"

typedef enum {
    FS_BRIDGE_FAT32 = 0,
    FS_BRIDGE_EXFAT,
    FS_BRIDGE_ISO9660,
    FS_BRIDGE_COUNT,
} fs_bridge_id_t;

/* Resolve + init the filesystem module for `id` (idempotent; safe to call
 * again after a module reload). Returns true once the module is mounted and
 * ready. */
bool fs_bridge_init(fs_bridge_id_t id);

/* The vfs_driver_t to hand to vfs_mount() for `id`. Always non-NULL for a
 * valid id; its fs_type/media_kind are only meaningful once fs_bridge_init()
 * has succeeded for that id. */
const vfs_driver_t *fs_bridge_vfs_driver(fs_bridge_id_t id);
