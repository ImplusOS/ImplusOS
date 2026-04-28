#pragma once

#include <stdint.h>

int32_t graphics_init(uint32_t window_id);

void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_present(void);

uint32_t get_display_width(void);
uint32_t get_display_height(void);
void *sys_get_display_framebuffer(void);
