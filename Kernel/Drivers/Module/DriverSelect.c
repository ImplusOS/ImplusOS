#include <stddef.h>
#include <stdint.h>

#include "DriverSelect.h"
#include "DriverBinary.h"
#include "DriverManager.h"

static driver_boot_framebuffer_t g_boot_framebuffer;

void driver_select_set_boot_framebuffer(const driver_boot_framebuffer_t *framebuffer) {
    if (!framebuffer) {
        g_boot_framebuffer.addr = NULL;
        g_boot_framebuffer.size_bytes = 0;
        g_boot_framebuffer.width = 0;
        g_boot_framebuffer.height = 0;
        g_boot_framebuffer.pixels_per_scan_line = 0;
        g_boot_framebuffer.bytes_per_pixel = 0;
        return;
    }

    g_boot_framebuffer = *framebuffer;
}

const driver_display_t *driver_select_pick_display_driver(void) {
    const driver_display_t *drv;
    const device_t *device;

    device = driver_manager_find(DRIVER_MANAGER_KIND_DISPLAY,
                                 "VirtIO_Driver.ELF");
    drv = device ? (const driver_display_t *)device->ops : NULL;
    if (drv) {
        if (!drv->probe || drv->probe()) {
            if (drv->init()) {
                return drv;
            }
        }
    }

    device = driver_manager_find(DRIVER_MANAGER_KIND_DISPLAY,
                                 "ImplusOS_Generic_Display_Driver.ELF");
    drv = device ? (const driver_display_t *)device->ops : NULL;
    if (drv) {
        if (drv->set_framebuffer) {
            drv->set_framebuffer(&g_boot_framebuffer);
        }
        if (!drv->probe || drv->probe()) {
            if (drv->init()) {
                return drv;
            }
        }
    }

    return NULL;
}
