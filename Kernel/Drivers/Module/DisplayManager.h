#pragma once

#include <stdbool.h>
#include <stdint.h>

bool display_manager_init(void);
bool display_manager_is_ready(void);
uint32_t display_manager_width(void);
uint32_t display_manager_height(void);
void display_manager_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t display_manager_get_pixel(uint32_t x, uint32_t y);
void display_manager_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void display_manager_present(void);
void *display_manager_get_framebuffer(void);
void display_manager_on_device_detached(const char *name);
