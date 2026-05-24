#include <stddef.h>

#include <string.h>

#include "Display_Main.h"
#include "Drivers/Server/Display/Display_Driver.h"
#include "Drivers/Module/DriverBinary.h"
#include "Drivers/Module/DriverManager.h"
#include "Drivers/Module/DriverSelect.h"
#include "MemoryManagement/Memory_Main.h"

const driver_display_t *g_active_display_driver = NULL;
static const char *g_active_display_module_name = NULL;

static uint32_t *g_framebuffer = NULL;
static uint32_t  g_fb_width    = 0;
static uint32_t  g_fb_height   = 0;
static uint64_t  g_framebuffer_pixels = 0;

static bool display_ensure_shadow_buffer(void)
{
    uint64_t total_pixels = (uint64_t)g_fb_width * (uint64_t)g_fb_height;
    if (total_pixels == 0u) {
        return false;
    }
    if (total_pixels > 0xFFFFFFFFULL / sizeof(uint32_t)) {
        return false;
    }

    if (g_framebuffer != NULL && g_framebuffer_pixels == total_pixels) {
        return true;
    }

    if (g_framebuffer != NULL) {
        free(g_framebuffer);
        g_framebuffer = NULL;
        g_framebuffer_pixels = 0u;
    }

    g_framebuffer = (uint32_t *)malloc((uint32_t)(total_pixels * sizeof(uint32_t)));
    if (g_framebuffer == NULL) {
        return false;
    }
    g_framebuffer_pixels = total_pixels;
    memset(g_framebuffer, 0xFF, (size_t)(total_pixels * sizeof(uint32_t)));
    return true;
}

static bool display_refresh_driver(void)
{
    if (g_active_display_driver != NULL && g_active_display_module_name != NULL) {
        return true;
    }
    if (g_active_display_driver != NULL && g_active_display_module_name != NULL) {
        const driver_display_t *current =
            driver_manager_get_display_driver(g_active_display_module_name);
        if (current == g_active_display_driver) {
            return true;
        }
    }

    g_active_display_driver = driver_select_pick_display_driver();
    if (g_active_display_driver == NULL) {
        g_active_display_module_name = NULL;
        g_fb_width = 0;
        g_fb_height = 0;
        return false;
    }

    if (g_active_display_driver ==
        driver_manager_get_display_driver("VirtIO_Driver.ELF")) {
        g_active_display_module_name = "VirtIO_Driver.ELF";
    } else if (g_active_display_driver ==
               driver_manager_get_display_driver("ImplusOS_Generic_Display_Driver.ELF")) {
        g_active_display_module_name = "ImplusOS_Generic_Display_Driver.ELF";
    } else {
        g_active_display_module_name = NULL;
    }

    g_fb_width = 0;
    g_fb_height = 0;
    if (g_active_display_driver->width != NULL) {
        g_fb_width = g_active_display_driver->width();
    }
    if (g_active_display_driver->height != NULL) {
        g_fb_height = g_active_display_driver->height();
    }
    return display_ensure_shadow_buffer();
}

bool display_init(void) {
    g_active_display_driver = NULL;
    g_active_display_module_name = NULL;
    if (!display_refresh_driver()) {
        return false;
    }

    if (g_fb_width == 0 || g_fb_height == 0) {
        return false;
    }

    return display_ensure_shadow_buffer();
}

bool display_is_ready(void) {
    if (!display_refresh_driver()) {
        return false;
    }

    if (g_active_display_driver->is_ready == NULL) {
        return false;
    }

    return g_active_display_driver->is_ready();
}

uint32_t display_width(void) {
    if (!display_is_ready() || g_active_display_driver->width == NULL) {
        return 0;
    }

    return g_active_display_driver->width();
}

uint32_t display_height(void) {
    if (!display_is_ready() || g_active_display_driver->height == NULL) {
        return 0;
    }

    return g_active_display_driver->height();
}

void display_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!display_is_ready() || g_active_display_driver->draw_pixel == NULL) {
        return;
    }

    if (x >= g_fb_width || y >= g_fb_height || !g_framebuffer) {
        return;
    }

    uint32_t idx = y * g_fb_width + x;

    if (g_framebuffer[idx] == color) {
        return;
    }

    g_framebuffer[idx] = color;
    g_active_display_driver->draw_pixel(x, y, color);
}

uint32_t display_get_pixel(uint32_t x, uint32_t y) {
    if (!display_is_ready()) {
        return 0;
    }

    if (x >= g_fb_width || y >= g_fb_height || !g_framebuffer) {
        return 0;
    }

    return g_framebuffer[y * g_fb_width + x];
}

void display_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!display_is_ready()) {
        return;
    }

    if (g_active_display_driver->fill_rect == NULL) {
        return;
    }

    if (!g_framebuffer || g_fb_width == 0 || g_fb_height == 0) {
        return;
    }

    if (x >= g_fb_width || y >= g_fb_height) {
        return;
    }
    if ((uint64_t)x + w > g_fb_width)  w = (uint32_t)(g_fb_width  - x);
    if ((uint64_t)y + h > g_fb_height) h = (uint32_t)(g_fb_height - y);
    if (w == 0 || h == 0) {
        return;
    }

    for (uint32_t yy = y; yy < y + h; yy++) {
        for (uint32_t xx = x; xx < x + w; xx++) {
            g_framebuffer[yy * g_fb_width + xx] = color;
        }
    }

    g_active_display_driver->fill_rect(x, y, w, h, color);
}

void display_present(void) {
    if (!display_is_ready() || g_active_display_driver->present == NULL) {
        return;
    }

    g_active_display_driver->present();
}

void *display_get_framebuffer(void) {
    if (!display_is_ready() || g_active_display_driver->get_framebuffer == NULL) {
        return NULL;
    }

    return g_active_display_driver->get_framebuffer();
}

void display_driver_detached(const char *module_name)
{
    if (module_name == NULL || g_active_display_module_name == NULL) {
        return;
    }
    if (strcmp(module_name, g_active_display_module_name) != 0) {
        return;
    }

    g_active_display_driver = NULL;
    g_active_display_module_name = NULL;
    if (g_framebuffer != NULL) {
        free(g_framebuffer);
        g_framebuffer = NULL;
    }
    g_framebuffer_pixels = 0u;
    g_fb_width = 0;
    g_fb_height = 0;
}
