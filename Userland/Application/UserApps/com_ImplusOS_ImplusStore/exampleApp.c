#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "exampleApp.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdio.h"

typedef struct {
    const char *name;
    const char *summary;
    const char *path;
} store_app_t;

static const store_app_t g_apps[] = {
    {"Editor", "Plain text editor with save support", "/Userland/UserApps/com_ImplusOS_editor/com_ImplusOS_editor.ELF"},
    {"File Manager", "Browse folders and inspect files", "/Userland/UserApps/com_ImplusOS_filemanager/com_ImplusOS_filemanager.ELF"},
    {"Process Manager", "Inspect and manage running processes", "/Userland/UserApps/com_ImplusOS_procman/com_ImplusOS_procman.ELF"},
    {"Network Test", "Run UDP and DNS smoke tests", "/Userland/UserApps/com_ImplusOS_NetworkTest/com_ImplusOS_NetworkTest.ELF"},
    {"Virtual Machine", "Boot the experimental KVM frontend", "/Userland/UserApps/com_ImplusOS_vm/com_ImplusOS_vm.ELF"},
};

static window_id_t g_win = 0;
static int g_selected = 0;
static char g_status[96] = "Select an app and press Enter to launch it.";

static void set_status(const char *status)
{
    strncpy(g_status, status ? status : "", sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = '\0';
}

static void render(void)
{
    window_clear(g_win);
    draw_fill_rect(0, 0, 620, 420, 0xFF111820);
    draw_fill_rect(0, 0, 620, 56, 0xFF1B3344);
    window_draw_text(g_win, 18, 14, "Implus Store", 0xFFEAF8FF, 20.0f);
    window_draw_text(g_win, 430, 20, "Enter launch  Q quit", 0xFFA9C6D4, 12.0f);

    for (uint32_t i = 0; i < sizeof(g_apps) / sizeof(g_apps[0]); ++i) {
        uint32_t y = 78u + i * 58u;
        if ((int)i == g_selected) {
            draw_fill_rect(12, y - 8u, 596, 48, 0xFF223D4E);
            draw_fill_rect(12, y - 8u, 4, 48, 0xFF5AC8E8);
        }
        window_draw_text(g_win, 28, y, g_apps[i].name, 0xFFEAF8FF, 15.0f);
        window_draw_text(g_win, 28, y + 20u, g_apps[i].summary, 0xFFA9C6D4, 12.0f);
    }

    draw_fill_rect(0, 376, 620, 44, 0xFF172633);
    window_draw_text(g_win, 16, 392, g_status, 0xFFFFD38A, 12.0f);
}

void exampleApp_main(void)
{
    g_win = window_create_ex(110, 80, 620, 420, 0xFF111820, "Implus Store");
    if (g_win == 0) return;
    window_subscribe_keyboard(g_win);
    graphics_init(g_win);
    render();
}

void _start(void)
{
    exampleApp_main();
    while (1) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0 && ev.pressed) {
            if (ev.keycode == 0x48 && g_selected > 0) {
                g_selected--;
                render();
            } else if (ev.keycode == 0x50 &&
                       g_selected < (int)(sizeof(g_apps) / sizeof(g_apps[0])) - 1) {
                g_selected++;
                render();
            } else if (ev.ascii == '\n' || ev.keycode == 0x1C) {
                int32_t pid = process_spawn(g_apps[g_selected].path);
                if (pid >= 0) set_status("Application launched.");
                else set_status("Failed to launch application.");
                render();
            } else if (ev.ascii == 'q' || ev.ascii == 'Q') {
                process_exit(0);
            }
        }
        process_yield();
    }
}
