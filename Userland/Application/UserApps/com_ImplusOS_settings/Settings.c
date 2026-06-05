#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "../../../API/Time.h"
#include "../../../API/SystemInfo.h"
#include "../../../API/IPC.h"
#include "../../../API/WM_Protocol.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdlib.h"
#include "../../../../libc/include/stdio.h"
#include <stdint.h>

#define WIN_X 100
#define WIN_Y 100
#define WIN_W 640
#define WIN_H 480

#define TAB_DEVICES    0
#define TAB_DISPLAY    1
#define TAB_APPEARANCE 2
#define TAB_USERS      3
#define TAB_SYSTEM     4
#define TAB_COUNT      5

#define COLOR_BG         0xFF1A1C1E
#define COLOR_SIDEBAR    0xFF232527
#define COLOR_ACCENT     0xFF3B82F6
#define COLOR_TEXT       0xFFEFF3F8
#define COLOR_DIM        0xFF94A3B8
#define COLOR_SEL_BG     0xFF2D3135

typedef struct {
    int x, y, w, h;
} rect_t;

static window_id_t g_win = 0;
static int g_current_tab = TAB_DEVICES;

static int g_mouse_x = 0;
static int g_mouse_y = 0;
static uint8_t g_mouse_btn = 0;
static uint8_t g_prev_mouse_btn = 0;

static inline bool rect_contains(rect_t r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static bool mouse_btn_pressed(uint8_t btn_mask) {
    return (g_mouse_btn & btn_mask) && !(g_prev_mouse_btn & btn_mask);
}

static void draw_sidebar(void) {
    const char *tabs[] = { "Devices", "Display", "Appearance", "Users", "System" };
    draw_fill_rect(0, 0, 160, WIN_H, COLOR_SIDEBAR);
    
    for (int i = 0; i < TAB_COUNT; i++) {
        uint32_t y = (uint32_t)(20 + i * 40);
        bool selected = (g_current_tab == i);
        if (selected) {
            draw_fill_rect(5, y, 150, 34, COLOR_SEL_BG);
            draw_fill_rect(0, y + 8, 4, 18, COLOR_ACCENT);
        }
        window_draw_text(g_win, 20, y + 10, tabs[i], selected ? COLOR_ACCENT : COLOR_TEXT, 14.0f);
    }
}

static void draw_devices(void) {
    uint32_t count = 0;
    os_get_device_count(&count);
    char buf[256];
    snprintf(buf, sizeof(buf), "Connected Devices (%u)", count);
    window_draw_text(g_win, 180, 20, buf, COLOR_TEXT, 18.0f);
    
    for (uint32_t i = 0; i < count && i < 15; i++) {
        system_device_t dev;
        if (os_get_device_info(i, &dev) == 0) {
            snprintf(buf, sizeof(buf), "%s", dev.device_name[0] ? dev.device_name : "Unknown Device");
            window_draw_text(g_win, 180, 60 + i * 25, buf, COLOR_TEXT, 13.0f);
            snprintf(buf, sizeof(buf), "Vendor: %04X, Device: %04X, IRQ: %u", dev.vendor_id, dev.device_id, dev.irq);
            window_draw_text(g_win, 450, 60 + i * 25, buf, COLOR_DIM, 11.0f);
        }
    }
}

static void draw_display(void) {
    system_graphics_info_t gfx;
    window_draw_text(g_win, 180, 20, "Display Settings", COLOR_TEXT, 18.0f);
    
    if (os_get_graphics_info(&gfx) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Vendor: %s", gfx.vendor);
        window_draw_text(g_win, 180, 60, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "Model: %s", gfx.model);
        window_draw_text(g_win, 180, 85, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "Resolution: %u x %u", gfx.display_width, gfx.display_height);
        window_draw_text(g_win, 180, 110, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "Color Depth: %u bpp", gfx.bits_per_pixel);
        window_draw_text(g_win, 180, 135, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "VRAM: %u MB", gfx.vram_mb);
        window_draw_text(g_win, 180, 160, buf, COLOR_TEXT, 14.0f);
    }
}

static void set_wm_theme(uint32_t bg_top, uint32_t bg_mid, uint32_t bg_bot, uint32_t accent, uint32_t tb_top, uint32_t tb_bot) {
    struct {
        wm_msg_header_t hdr;
        uint32_t bg_top;
        uint32_t bg_mid;
        uint32_t bg_bot;
        uint32_t accent;
        uint32_t titlebar_top;
        uint32_t titlebar_bot;
    } msg;
    msg.hdr.type = WM_SET_THEME;
    msg.hdr.window_id = 0;
    msg.hdr.request_id = 0;
    msg.bg_top = bg_top;
    msg.bg_mid = bg_mid;
    msg.bg_bot = bg_bot;
    msg.accent = accent;
    msg.titlebar_top = tb_top;
    msg.titlebar_bot = tb_bot;
    
    int32_t wm_pid = window_get_wm_pid();
    if (wm_pid > 0) {
        ipc_send_message(wm_pid, &msg, sizeof(msg));
    }
}

static void draw_appearance(void) {
    window_draw_text(g_win, 180, 20, "Appearance", COLOR_TEXT, 18.0f);
    
    window_draw_text(g_win, 180, 60, "Presets:", COLOR_TEXT, 14.0f);
    
    draw_fill_rect(180, 90, 100, 40, 0xFF3B82F6);
    window_draw_text(g_win, 195, 103, "Default", 0xFFFFFFFF, 12.0f);
    
    draw_fill_rect(300, 90, 100, 40, 0xFFE91E63);
    window_draw_text(g_win, 315, 103, "Pink", 0xFFFFFFFF, 12.0f);
    
    draw_fill_rect(420, 90, 100, 40, 0xFF4CAF50);
    window_draw_text(g_win, 435, 103, "Green", 0xFFFFFFFF, 12.0f);
    
    window_draw_text(g_win, 180, 160, "Background:", COLOR_TEXT, 14.0f);
    draw_fill_rect(180, 190, 150, 40, COLOR_SIDEBAR);
    window_draw_text(g_win, 200, 203, "Reload BG Image", COLOR_TEXT, 12.0f);
}

static void draw_users(void) {
    window_draw_text(g_win, 180, 20, "User Accounts", COLOR_TEXT, 18.0f);
    
    window_draw_text(g_win, 180, 60, "root (UID: 0)", COLOR_TEXT, 14.0f);
    window_draw_text(g_win, 180, 85, "implus (UID: 1000)", COLOR_TEXT, 14.0f);
    
    draw_fill_rect(180, 120, 120, 30, COLOR_ACCENT);
    window_draw_text(g_win, 195, 128, "Add User", 0xFFFFFFFF, 12.0f);
}

static void draw_system(void) {
    window_draw_text(g_win, 180, 20, "System Information", COLOR_TEXT, 18.0f);
    
    system_info_t info;
    if (os_get_system_info(&info) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "OS: ImplusOS 64-bit (%s)", info.arch.name);
        window_draw_text(g_win, 180, 60, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "CPU: %s", info.cpu.brand);
        window_draw_text(g_win, 180, 85, buf, COLOR_TEXT, 14.0f);
        snprintf(buf, sizeof(buf), "Memory: %llu MB used / %llu MB total", 
                 info.memory.used_bytes / (1024*1024), info.memory.total_bytes / (1024*1024));
        window_draw_text(g_win, 180, 110, buf, COLOR_TEXT, 14.0f);
    }
    
    draw_fill_rect(180, 200, 200, 40, 0xFFEB5757);
    window_draw_text(g_win, 210, 213, "Reset ImplusOS", 0xFFFFFFFF, 14.0f);
    window_draw_text(g_win, 180, 250, "Wipe all data and reinstall from media", COLOR_DIM, 11.0f);
}

static void draw(void) {
    window_clear(g_win);
    draw_fill_rect(0, 0, WIN_W, WIN_H, COLOR_BG);
    
    draw_sidebar();
    
    switch (g_current_tab) {
        case TAB_DEVICES: draw_devices(); break;
        case TAB_DISPLAY: draw_display(); break;
        case TAB_APPEARANCE: draw_appearance(); break;
        case TAB_USERS: draw_users(); break;
        case TAB_SYSTEM: draw_system(); break;
    }
    
    draw_present();
}

static void handle_click(int x, int y) {
    if (x < 160) {
        for (int i = 0; i < TAB_COUNT; i++) {
            if (y >= 20 + i * 40 && y < 54 + i * 40) {
                g_current_tab = i;
                return;
            }
        }
    }
    
    if (g_current_tab == TAB_APPEARANCE) {
        if (y >= 90 && y < 130) {
            if (x >= 180 && x < 280) {
                set_wm_theme(0xFF0D1117, 0xFF161B22, 0xFF1C2433, 0xFF3B82F6, 0xF51E293B, 0xF5182232);
            } else if (x >= 300 && x < 400) {
                set_wm_theme(0xFF1A0D17, 0xFF22161B, 0xFF331C24, 0xFFE91E63, 0xF53B1E29, 0xF5321822);
            } else if (x >= 420 && x < 520) {
                set_wm_theme(0xFF0D1A11, 0xFF16221B, 0xFF1C3324, 0xFF4CAF50, 0xF51E3B29, 0xF5183222);
            }
        } else if (y >= 190 && y < 230 && x >= 180 && x < 330) {
            struct { wm_msg_header_t hdr; } msg;
            msg.hdr.type = WM_RELOAD_BACKGROUND;
            int32_t wm_pid = window_get_wm_pid();
            if (wm_pid > 0) ipc_send_message(wm_pid, &msg, sizeof(msg));
        }
    } else if (g_current_tab == TAB_SYSTEM) {
        if (x >= 180 && x < 380 && y >= 200 && y < 240) {
            system_reboot();
        }
    }
}

int main(void) {
    g_win = window_create_ex(WIN_X, WIN_Y, WIN_W, WIN_H, COLOR_BG, "Settings");
    if (g_win == 0) return 1;
    
    window_subscribe_mouse(g_win);
    graphics_init(g_win);
    
    draw();
    
    while (1) {
        input_mouse_event_t mev;
        if (window_input_mouse_poll(&mev) > 0) {
            g_prev_mouse_btn = g_mouse_btn;
            g_mouse_btn = mev.buttons;
            g_mouse_x = (int)(int16_t)mev.x;
            g_mouse_y = (int)(int16_t)mev.y;
            
            if (mouse_btn_pressed(INPUT_MOUSE_BTN_LEFT)) {
                handle_click(g_mouse_x, g_mouse_y);
                draw();
            }
        }
        process_yield();
    }
    
    return 0;
}

void _start(void) {
    process_exit((int)main());
}
