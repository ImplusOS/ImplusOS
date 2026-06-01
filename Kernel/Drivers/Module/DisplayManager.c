#include "DisplayManager.h"

#include "Drivers/Client/Display/Display_Main.h"

bool display_manager_init(void)
{
    return display_init();
}

bool display_manager_is_ready(void)
{
    return display_is_ready();
}

uint32_t display_manager_width(void)
{
    return display_width();
}

uint32_t display_manager_height(void)
{
    return display_height();
}

void display_manager_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    display_draw_pixel(x, y, color);
}

uint32_t display_manager_get_pixel(uint32_t x, uint32_t y)
{
    return display_get_pixel(x, y);
}

void display_manager_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    display_fill_rect(x, y, w, h, color);
}

void display_manager_present(void)
{
    display_present();
}

void *display_manager_get_framebuffer(void)
{
    return display_get_framebuffer();
}

void display_manager_on_device_detached(const char *name)
{
    display_driver_detached(name);
}
