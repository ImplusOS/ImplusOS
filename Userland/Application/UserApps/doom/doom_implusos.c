#include <Graphics.h>
#include <Input.h>
#include <Process.h>
#include <Window.h>

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#define KEYQUEUE_SIZE 64u
#define DOOM_WINDOW_X 120u
#define DOOM_WINDOW_Y 80u

static window_id_t g_window;
static uint32_t *g_framebuffer;
static uint32_t g_framebuffer_width;
static uint32_t g_framebuffer_height;
static uint16_t g_key_queue[KEYQUEUE_SIZE];
static unsigned int g_key_queue_write;
static unsigned int g_key_queue_read;

static unsigned char keycode_to_doom_key(uint16_t keycode, uint8_t ascii)
{
    switch (keycode) {
    case 0x01u:
        return KEY_ESCAPE;
    case 0x02u:
        return '1';
    case 0x03u:
        return '2';
    case 0x04u:
        return '3';
    case 0x05u:
        return '4';
    case 0x06u:
        return '5';
    case 0x07u:
        return '6';
    case 0x08u:
        return '7';
    case 0x09u:
        return '8';
    case 0x0Au:
        return '9';
    case 0x0Bu:
        return '0';
    case 0x0Cu:
    case 0x4Au:
        return KEY_MINUS;
    case 0x0Du:
    case 0x4Eu:
        return KEY_EQUALS;
    case 0x0Eu:
        return KEY_BACKSPACE;
    case 0x0Fu:
        return KEY_TAB;
    case 0x1Cu:
    case 0xE01Cu:
        return KEY_ENTER;
    case 0x1Du:
    case 0xE01Du:
        return KEY_FIRE;
    case 0x2Au:
    case 0x36u:
        return KEY_RSHIFT;
    case 0x38u:
    case 0xE038u:
        return KEY_LALT;
    case 0x39u:
        return KEY_USE;
    case 0x3Bu:
        return KEY_F1;
    case 0x3Cu:
        return KEY_F2;
    case 0x3Du:
        return KEY_F3;
    case 0x3Eu:
        return KEY_F4;
    case 0x3Fu:
        return KEY_F5;
    case 0x40u:
        return KEY_F6;
    case 0x41u:
        return KEY_F7;
    case 0x42u:
        return KEY_F8;
    case 0x43u:
        return KEY_F9;
    case 0x44u:
        return KEY_F10;
    case 0x47u:
    case 0xE047u:
        return KEY_HOME;
    case 0x48u:
    case 0xE048u:
        return KEY_UPARROW;
    case 0x49u:
    case 0xE049u:
        return KEY_PGUP;
    case 0x4Bu:
    case 0xE04Bu:
        return KEY_LEFTARROW;
    case 0x4Du:
    case 0xE04Du:
        return KEY_RIGHTARROW;
    case 0x4Fu:
    case 0xE04Fu:
        return KEY_END;
    case 0x50u:
    case 0xE050u:
        return KEY_DOWNARROW;
    case 0x51u:
    case 0xE051u:
        return KEY_PGDN;
    case 0x52u:
    case 0xE052u:
        return KEY_INS;
    case 0x53u:
    case 0xE053u:
        return KEY_DEL;
    case 0x57u:
        return KEY_F11;
    case 0x58u:
        return KEY_F12;
    default:
        break;
    }

    if (ascii >= 'A' && ascii <= 'Z') {
        return (unsigned char)tolower((int)ascii);
    }
    if (ascii >= 0x20u && ascii <= 0x7Eu) {
        return ascii;
    }

    return 0;
}

static void queue_key(int pressed, unsigned char doom_key)
{
    if (doom_key == 0u) {
        return;
    }

    g_key_queue[g_key_queue_write] = (uint16_t)(((pressed != 0) ? 1u : 0u) << 8u) |
                                     (uint16_t)doom_key;
    g_key_queue_write = (g_key_queue_write + 1u) % KEYQUEUE_SIZE;

    if (g_key_queue_write == g_key_queue_read) {
        g_key_queue_read = (g_key_queue_read + 1u) % KEYQUEUE_SIZE;
    }
}

static void poll_input(void)
{
    input_keyboard_event_t key_event;
    input_mouse_event_t mouse_event;

    while (window_input_keyboard_poll(&key_event) > 0) {
        unsigned char doom_key =
            keycode_to_doom_key(key_event.keycode, key_event.ascii);
        queue_key(key_event.pressed != 0u, doom_key);
    }

    while (window_input_mouse_poll(&mouse_event) > 0) {
        (void)mouse_event;
    }
}

static void yield_window_manager(uint32_t iterations)
{
    for (uint32_t i = 0u; i < iterations; ++i) {
        sleep_ms(16u);
        process_yield();
    }
}

static void clear_window(uint32_t color)
{
    if (g_framebuffer == NULL) {
        return;
    }

    for (uint32_t y = 0u; y < g_framebuffer_height; ++y) {
        uint32_t *row = &g_framebuffer[y * g_framebuffer_width];
        for (uint32_t x = 0u; x < g_framebuffer_width; ++x) {
            row[x] = color;
        }
    }
    window_damage(g_window, 0u, 0u, g_framebuffer_width, g_framebuffer_height);
}

static void copy_screen_to_window(void)
{
    uint32_t copy_width;
    uint32_t copy_height;

    if (g_framebuffer == NULL || DG_ScreenBuffer == NULL) {
        return;
    }

    copy_width = g_framebuffer_width;
    if (copy_width > DOOMGENERIC_RESX) {
        copy_width = DOOMGENERIC_RESX;
    }

    copy_height = g_framebuffer_height;
    if (copy_height > DOOMGENERIC_RESY) {
        copy_height = DOOMGENERIC_RESY;
    }

    for (uint32_t y = 0u; y < copy_height; ++y) {
        const uint32_t *src = &DG_ScreenBuffer[y * DOOMGENERIC_RESX];
        uint32_t *dst = &g_framebuffer[y * g_framebuffer_width];

        for (uint32_t x = 0u; x < copy_width; ++x) {
            dst[x] = src[x] | 0xFF000000u;
        }
    }
}

void DG_Init(void)
{
    g_window = window_create_ex(DOOM_WINDOW_X, DOOM_WINDOW_Y,
                                DOOMGENERIC_RESX, DOOMGENERIC_RESY,
                                0xFF000000u, "DOOM");
    if (g_window == 0u) {
        process_exit(1);
    }

    (void)window_subscribe_keyboard(g_window);
    (void)window_subscribe_mouse(g_window);
    window_show(g_window);
    window_raise(g_window);
    window_set_focus(g_window);

    g_framebuffer = window_get_backing_store(g_window, &g_framebuffer_width,
                                             &g_framebuffer_height);
    if (g_framebuffer == NULL || g_framebuffer_width == 0u ||
        g_framebuffer_height == 0u) {
        process_exit(1);
    }

    clear_window(0xFF000000u);
    yield_window_manager(2u);
}

void DG_DrawFrame(void)
{
    copy_screen_to_window();
    window_damage(g_window, 0u, 0u, g_framebuffer_width, g_framebuffer_height);
    poll_input();
    process_yield();
}

void DG_SleepMs(uint32_t ms)
{
    sleep_ms(ms);
    process_yield();
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)get_uptime_ms();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    poll_input();

    if (pressed == NULL || key == NULL ||
        g_key_queue_read == g_key_queue_write) {
        return 0;
    }

    uint16_t value = g_key_queue[g_key_queue_read];
    g_key_queue_read = (g_key_queue_read + 1u) % KEYQUEUE_SIZE;

    *pressed = (int)(value >> 8u);
    *key = (unsigned char)(value & 0xffu);
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}
