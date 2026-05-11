#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "DriverBinary.h"
#include "DriverManager.h"
#include "kernel/boot_info.h"

void driver_module_manager_init(const BOOT_INFO *boot_info);
const driver_binary_t *driver_module_manager_kernel_api(void);
void *driver_module_manager_get_driver(const char *name);
bool driver_module_init_all(void);
bool driver_module_manager_unload_by_name(const char *name);
bool driver_module_manager_reload_by_name(const char *name);
