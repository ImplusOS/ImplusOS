#pragma once

/*
 * FAT32_VFS_Bridge -- kernel-resident glue between the loaded FAT32_Driver.ELF
 * module and Core/vfs/VFS.c, consolidated from what used to be two separate
 * files (FAT32_Client.c, a thin driver_manager_find() proxy, and
 * FAT32_VFS_Adapter.c, a vfs_file_t<->FAT32_FILE type converter) -- see
 * Docs/Others/TODO_OS_Refactor.md 6.2 for why. Only two symbols are used
 * outside this file: fat32_init() (called once from kernel_main.c's
 * all_fs_initialize()) and fat32_vfs_get_driver() (used to obtain the
 * vfs_driver_t handed to vfs_mount()).
 */

#include <stdbool.h>

#include "kernel/interfaces/vfs_types.h"

bool fat32_init(void);
const vfs_driver_t *fat32_vfs_get_driver(void);
