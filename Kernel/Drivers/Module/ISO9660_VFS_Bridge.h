#pragma once

/*
 * ISO9660_VFS_Bridge -- kernel-resident glue between the loaded
 * ISO9660_Driver.ELF module and Core/vfs/VFS.c, consolidated from what used
 * to be two separate files (ISO9660_Client.c, a thin driver_manager_find()
 * proxy, and ISO9660_VFS_Adapter.c, a vfs_file_t<->ISO9660_FILE type
 * converter) -- see Docs/Others/TODO_OS_Refactor.md 6.2 and
 * FAT32_VFS_Bridge.h (the same pattern). Only two symbols are used outside
 * this file: iso9660_init() (called once from kernel_main.c's
 * all_fs_initialize()) and iso9660_vfs_get_driver() (used to obtain the
 * vfs_driver_t handed to vfs_mount()).
 */

#include <stdbool.h>

#include "kernel/interfaces/vfs_types.h"

bool iso9660_init(void);
const vfs_driver_t *iso9660_vfs_get_driver(void);
