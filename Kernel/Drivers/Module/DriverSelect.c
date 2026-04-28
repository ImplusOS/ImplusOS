#include <stddef.h>
#include <stdint.h>

#include "DriverSelect.h"
#include "DriverBinary.h"
#include "DriverManager.h"
#include "Drivers/DrvMain/Server/Display/ImplusOS_Generic/ImplusOS_Generic.h"

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

static const driver_display_t g_boot_fb_driver = {
    .name            = "BootFramebuffer",
    .probe           = fb_probe,
    .init            = fb_init,
    .is_ready        = fb_is_ready,
    .width           = fb_width,
    .height          = fb_height,
    .draw_pixel      = fb_draw_pixel,
    .fill_rect       = fb_fill_rect,
    .present         = fb_present,
    .set_framebuffer = generic_fb_set,
};

const driver_display_t *driver_select_pick_display_driver(void) {
    const driver_display_t *drv;

    drv = driver_manager_get_display_driver("VirtIO_Driver.ELF");
    if (drv) {
        if (!drv->probe || drv->probe()) {
            if (drv->init()) {
                return drv;
            }
        }
    }

    drv = driver_manager_get_display_driver("ImplusOS_Generic_Display_Driver.ELF");
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
    
    if (g_boot_fb_driver.set_framebuffer) {
        g_boot_fb_driver.set_framebuffer(&g_boot_framebuffer);
    }
    if (!g_boot_fb_driver.probe || g_boot_fb_driver.probe()) {
        if (g_boot_fb_driver.init()) {
            return &g_boot_fb_driver;
        }
    }

    return NULL;
}
