#include <stdint.h>
#include <string.h>
#include "Window.h"
#include "Graphics.h"
#include "Process.h"

void _start(void) {
    uint32_t width = 360;
    uint32_t height = 180;
    uint32_t screen_w = get_display_width();
    uint32_t screen_h = get_display_height();
    uint32_t x = (screen_w - width) / 2;
    uint32_t y = (screen_h - height) / 2;

    window_id_t wid = window_create_ex(x, y, width, height, 0x1E1E2E, "About ImplusOS");

    if (wid != 0) {
        window_draw_text(wid, 20, 30, "ImplusOS", 0x89B4FA, 32.0f);
        window_draw_text(wid, 20, 80, "Version 0.02 (Alpha)", 0xCDD6F4, 18.0f);
        
    }

    while (1) {
        process_yield();
    }
}
