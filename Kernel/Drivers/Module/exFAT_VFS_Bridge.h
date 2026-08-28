#pragma once
#include <stdbool.h>
#include "kernel/interfaces/vfs_types.h"

bool exfat_init(void);
const vfs_driver_t *exfat_vfs_get_driver(void);
