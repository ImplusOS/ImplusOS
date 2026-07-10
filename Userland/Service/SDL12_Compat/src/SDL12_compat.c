#include <SDL/SDL.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Userland/API/Input.h"
#include "Userland/API/Process.h"
#include "Userland/API/Window.h"

#define SDL12_QUEUE_SIZE 64u
#define SDL12_MAX_TIMERS 16u

typedef struct {
    bool active;
    SDL_TimerID id;
    Uint32 interval;
    uint64_t deadline_ms;
    SDL_NewTimerCallback callback;
    void *param;
} sdl12_timer_t;

static SDL_PixelFormat g_format;
static SDL_Surface g_surface;
static window_id_t g_window;
static Uint32 g_init_flags;
static char g_error[96];
static SDL_Event g_queue[SDL12_QUEUE_SIZE];
static uint32_t g_queue_head;
static uint32_t g_queue_tail;
static uint8_t g_mouse_buttons;
static uint16_t g_mouse_x;
static uint16_t g_mouse_y;
static sdl12_timer_t g_timers[SDL12_MAX_TIMERS];
static SDL_TimerID g_next_timer_id = 1;
static bool g_surface_opaque = false;

static bool g_dirty_active = false;
static Sint32 g_dirty_x1 = 0;
static Sint32 g_dirty_y1 = 0;
static Sint32 g_dirty_x2 = 0;
static Sint32 g_dirty_y2 = 0;
static uint64_t g_dirty_last_flush_ms = 0u;
static void flush_dirty_rect(void);

static void sdl12_set_error(const char *msg)
{
    if (!msg) {
        msg = "SDL12_Compat error";
    }
    strlcpy(g_error, msg, sizeof(g_error));
}

const char *SDL_GetError(void)
{
    return g_error;
}

static bool queue_is_empty(void)
{
    return g_queue_head == g_queue_tail;
}

static bool queue_is_full(void)
{
    return ((g_queue_tail + 1u) % SDL12_QUEUE_SIZE) == g_queue_head;
}

int SDL_PushEvent(SDL_Event *event)
{
    if (!event) {
        sdl12_set_error("SDL_PushEvent: null event");
        return -1;
    }
    if (queue_is_full()) {
        sdl12_set_error("SDL event queue full");
        return -1;
    }

    g_queue[g_queue_tail] = *event;
    g_queue_tail = (g_queue_tail + 1u) % SDL12_QUEUE_SIZE;
    return 0;
}

static int queue_pop(SDL_Event *event)
{
    if (queue_is_empty()) {
        return 0;
    }
    if (event) {
        *event = g_queue[g_queue_head];
    }
    g_queue_head = (g_queue_head + 1u) % SDL12_QUEUE_SIZE;
    return 1;
}

static SDLKey translate_key(uint16_t keycode, uint8_t ascii)
{
    if (ascii >= 0x20u && ascii < 0x7fu) {
        return (SDLKey)ascii;
    }

    switch (keycode) {
    case 0x01: return SDLK_ESCAPE;
    case 0x02: return '1';
    case 0x03: return '2';
    case 0x04: return '3';
    case 0x05: return '4';
    case 0x06: return '5';
    case 0x07: return '6';
    case 0x08: return '7';
    case 0x09: return '8';
    case 0x0a: return '9';
    case 0x0b: return '0';
    case 0x0c: return '-';
    case 0x0d: return '=';
    case 0x0e: return SDLK_BACKSPACE;
    case 0x0f: return SDLK_TAB;
    case 0x10: return 'q';
    case 0x11: return 'w';
    case 0x12: return 'e';
    case 0x13: return 'r';
    case 0x14: return 't';
    case 0x15: return 'y';
    case 0x16: return 'u';
    case 0x17: return 'i';
    case 0x18: return 'o';
    case 0x19: return 'p';
    case 0x1a: return '[';
    case 0x1b: return ']';
    case 0x1c: return SDLK_RETURN;
    case 0x1d: return SDLK_LCTRL;
    case 0x1e: return 'a';
    case 0x1f: return 's';
    case 0x20: return 'd';
    case 0x21: return 'f';
    case 0x22: return 'g';
    case 0x23: return 'h';
    case 0x24: return 'j';
    case 0x25: return 'k';
    case 0x26: return 'l';
    case 0x27: return ';';
    case 0x28: return '\'';
    case 0x29: return '`';
    case 0x2a: return SDLK_LSHIFT;
    case 0x2b: return '\\';
    case 0x2c: return 'z';
    case 0x2d: return 'x';
    case 0x2e: return 'c';
    case 0x2f: return 'v';
    case 0x30: return 'b';
    case 0x31: return 'n';
    case 0x32: return 'm';
    case 0x33: return ',';
    case 0x34: return '.';
    case 0x35: return '/';
    case 0x36: return SDLK_RSHIFT;
    case 0x37: return SDLK_KP_MULTIPLY;
    case 0x38: return SDLK_LALT;
    case 0x39: return SDLK_SPACE;
    case 0x3a: return SDLK_CAPSLOCK;
    case 0x3b: return SDLK_F1;
    case 0x3c: return SDLK_F2;
    case 0x3d: return SDLK_F3;
    case 0x3e: return SDLK_F4;
    case 0x3f: return SDLK_F5;
    case 0x40: return SDLK_F6;
    case 0x41: return SDLK_F7;
    case 0x42: return SDLK_F8;
    case 0x43: return SDLK_F9;
    case 0x44: return SDLK_F10;
    case 0x47: return SDLK_KP7;
    case 0x48: return SDLK_KP8;
    case 0x49: return SDLK_KP9;
    case 0x4a: return SDLK_KP_MINUS;
    case 0x4b: return SDLK_KP4;
    case 0x4c: return SDLK_KP5;
    case 0x4d: return SDLK_KP6;
    case 0x4e: return SDLK_KP_PLUS;
    case 0x4f: return SDLK_KP1;
    case 0x50: return SDLK_KP2;
    case 0x51: return SDLK_KP3;
    case 0x52: return SDLK_KP0;
    case 0x53: return SDLK_KP_PERIOD;
    case 0x57: return SDLK_F11;
    case 0x58: return SDLK_F12;
    case 0xe01c: return SDLK_KP_ENTER;
    case 0xe01d: return SDLK_RCTRL;
    case 0xe035: return SDLK_KP_DIVIDE;
    case 0xe038: return SDLK_RALT;
    case 0xe047: return SDLK_HOME;
    case 0xe048: return SDLK_UP;
    case 0xe049: return SDLK_PAGEUP;
    case 0xe04b: return SDLK_LEFT;
    case 0xe04d: return SDLK_RIGHT;
    case 0xe04f: return SDLK_END;
    case 0xe050: return SDLK_DOWN;
    case 0xe051: return SDLK_PAGEDOWN;
    case 0xe052: return SDLK_INSERT;
    case 0xe053: return SDLK_DELETE;
    default: return 0;
    }
}

static void push_key_event(const input_keyboard_event_t *input)
{
    SDLKey key = translate_key(input->keycode, input->ascii);
    if (key == 0) {
        return;
    }

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = input->pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.type = event.type;
    event.key.state = input->pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = key;
    event.key.keysym.scancode = (Uint8)(input->keycode & 0xffu);
    event.key.keysym.unicode = input->ascii;
    (void)SDL_PushEvent(&event);
}

static void push_button_event(Uint8 type, Uint8 button, uint16_t x, uint16_t y)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.button.type = type;
    event.button.button = button;
    event.button.state = (type == SDL_MOUSEBUTTONDOWN) ? SDL_PRESSED : SDL_RELEASED;
    event.button.x = x;
    event.button.y = y;
    (void)SDL_PushEvent(&event);
}

static void push_motion_event(uint16_t x, uint16_t y)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.type = SDL_MOUSEMOTION;
    event.motion.state = g_mouse_buttons;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.xrel = (Sint16)((int)x - (int)g_mouse_x);
    event.motion.yrel = (Sint16)((int)y - (int)g_mouse_y);
    g_mouse_x = x;
    g_mouse_y = y;
    (void)SDL_PushEvent(&event);
}

static void push_button_delta(uint8_t old_buttons, uint8_t new_buttons,
                              uint8_t mask, Uint8 button)
{
    bool was_down = (old_buttons & mask) != 0;
    bool is_down = (new_buttons & mask) != 0;
    if (was_down == is_down) {
        return;
    }
    push_button_event(is_down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP,
                      button, g_mouse_x, g_mouse_y);
}

static void pump_window_events(void)
{
    input_keyboard_event_t key_event;
    while (window_input_keyboard_poll(&key_event) > 0) {
        push_key_event(&key_event);
    }

    input_mouse_event_t mouse_event;
    while (window_input_mouse_poll(&mouse_event) > 0) {
        if (mouse_event.x != g_mouse_x || mouse_event.y != g_mouse_y) {
            push_motion_event(mouse_event.x, mouse_event.y);
        }

        push_button_delta(g_mouse_buttons, mouse_event.buttons,
                          INPUT_MOUSE_BTN_LEFT, SDL_BUTTON_LEFT);
        push_button_delta(g_mouse_buttons, mouse_event.buttons,
                          INPUT_MOUSE_BTN_MIDDLE, SDL_BUTTON_MIDDLE);
        push_button_delta(g_mouse_buttons, mouse_event.buttons,
                          INPUT_MOUSE_BTN_RIGHT, SDL_BUTTON_RIGHT);
        g_mouse_buttons = mouse_event.buttons;

        if (mouse_event.wheel > 0) {
            push_button_event(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_WHEELUP,
                              mouse_event.x, mouse_event.y);
            push_button_event(SDL_MOUSEBUTTONUP, SDL_BUTTON_WHEELUP,
                              mouse_event.x, mouse_event.y);
        } else if (mouse_event.wheel < 0) {
            push_button_event(SDL_MOUSEBUTTONDOWN, SDL_BUTTON_WHEELDOWN,
                              mouse_event.x, mouse_event.y);
            push_button_event(SDL_MOUSEBUTTONUP, SDL_BUTTON_WHEELDOWN,
                              mouse_event.x, mouse_event.y);
        }
    }
}

static void process_timers(void)
{
    uint64_t now = get_uptime_ms();

    for (size_t i = 0; i < SDL12_MAX_TIMERS; ++i) {
        sdl12_timer_t *timer = &g_timers[i];
        if (!timer->active || timer->deadline_ms > now) {
            continue;
        }

        Uint32 next = 0;
        if (timer->callback) {
            next = timer->callback(timer->interval, timer->param);
        }
        if (next == 0) {
            timer->active = false;
        } else {
            timer->interval = next;
            timer->deadline_ms = now + next;
        }
    }
}

static void make_updated_pixels_opaque(SDL_Surface *screen,
                                       Sint32 x, Sint32 y,
                                       Uint32 w, Uint32 h)
{
    if (!screen || !screen->pixels || !screen->format ||
        screen->format->BytesPerPixel != sizeof(uint32_t) ||
        screen->format->Amask == 0u) {
        return;
    }

    uint32_t alpha_mask = screen->format->Amask;
    uint8_t *base = (uint8_t *)screen->pixels + (size_t)y * (size_t)screen->pitch;
    for (Uint32 row = 0; row < h; ++row) {
        uint32_t *pixels = (uint32_t *)(void *)(base + (size_t)row * (size_t)screen->pitch);
        pixels += (size_t)x;
        for (Uint32 col = 0; col < w; ++col) {
            pixels[col] |= alpha_mask;
        }
    }
}

int SDL_Init(Uint32 flags)
{
    g_init_flags |= flags;
    g_error[0] = '\0';
    return 0;
}

void SDL_Quit(void)
{
    if (g_window != 0) {
        window_release_backing_store(g_window);
        window_destroy(g_window);
    }
    memset(&g_surface, 0, sizeof(g_surface));
    memset(&g_format, 0, sizeof(g_format));
    memset(g_timers, 0, sizeof(g_timers));
    g_window = 0;
    g_init_flags = 0;
    g_queue_head = 0;
    g_queue_tail = 0;
    g_mouse_buttons = 0;
    g_surface_opaque = false;
    g_dirty_active = false;
    g_dirty_last_flush_ms = 0u;
}

static void setup_format(int bpp)
{
    memset(&g_format, 0, sizeof(g_format));
    g_format.BitsPerPixel = (Uint8)(bpp > 0 ? bpp : 32);
    g_format.BytesPerPixel = (Uint8)((g_format.BitsPerPixel + 7u) / 8u);
    g_format.Rmask = 0x00ff0000u;
    g_format.Gmask = 0x0000ff00u;
    g_format.Bmask = 0x000000ffu;
    g_format.Amask = 0xff000000u;
    g_format.Rshift = 16;
    g_format.Gshift = 8;
    g_format.Bshift = 0;
    g_format.Ashift = 24;
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    if ((g_init_flags & SDL_INIT_VIDEO) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            return NULL;
        }
    }
    if (width <= 0 || height <= 0) {
        sdl12_set_error("SDL_SetVideoMode: invalid geometry");
        return NULL;
    }

    if (g_window == 0) {
        g_window = window_create_ex(120, 80, (uint32_t)width, (uint32_t)height,
                                    0xff000000u, "NetSurf");
        if (g_window == 0) {
            sdl12_set_error("window_create_ex failed");
            return NULL;
        }
        (void)window_subscribe_keyboard(g_window);
        (void)window_subscribe_mouse(g_window);
        g_surface_opaque = window_set_surface_opaque(g_window, true) >= 0;
        window_show(g_window);
        window_raise(g_window);
        window_set_focus(g_window);
    } else {
        uint32_t x = 120;
        uint32_t y = 80;
        uint32_t old_w = 0;
        uint32_t old_h = 0;
        (void)window_get_rect(g_window, &x, &y, &old_w, &old_h);
        window_release_backing_store(g_window);
        window_set_rect(g_window, x, y, (uint32_t)width, (uint32_t)height);
    }

    uint32_t fb_w = 0;
    uint32_t fb_h = 0;
    uint32_t *pixels = window_get_backing_store(g_window, &fb_w, &fb_h);
    if (!pixels || fb_w == 0 || fb_h == 0) {
        sdl12_set_error("window_get_backing_store failed");
        return NULL;
    }

    setup_format(bpp == 0 ? 32 : bpp);
    g_surface.flags = flags;
    g_surface.format = &g_format;
    g_surface.w = (int)fb_w;
    g_surface.h = (int)fb_h;
    g_surface.pitch = (int)(fb_w * sizeof(uint32_t));
    g_surface.pixels = pixels;

    SDL_Event resize_event;
    memset(&resize_event, 0, sizeof(resize_event));
    resize_event.type = SDL_VIDEORESIZE;
    resize_event.resize.type = SDL_VIDEORESIZE;
    resize_event.resize.w = g_surface.w;
    resize_event.resize.h = g_surface.h;
    (void)SDL_PushEvent(&resize_event);

    return &g_surface;
}

int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst || !src->pixels || !dst->pixels) {
        sdl12_set_error("SDL_BlitSurface: invalid surface");
        return -1;
    }

    SDL_Rect s = { 0, 0, src->w, src->h };
    SDL_Rect d = { 0, 0, src->w, src->h };
    if (srcrect) {
        s = *srcrect;
    }
    if (dstrect) {
        d.x = dstrect->x;
        d.y = dstrect->y;
    }
    if (s.w <= 0 || s.h <= 0) {
        return 0;
    }

    if (s.x < 0) {
        d.x -= s.x;
        s.w += s.x;
        s.x = 0;
    }
    if (s.y < 0) {
        d.y -= s.y;
        s.h += s.y;
        s.y = 0;
    }
    if (d.x < 0) {
        s.x -= d.x;
        s.w += d.x;
        d.x = 0;
    }
    if (d.y < 0) {
        s.y -= d.y;
        s.h += d.y;
        d.y = 0;
    }
    if (s.x + s.w > src->w) {
        s.w = src->w - s.x;
    }
    if (s.y + s.h > src->h) {
        s.h = src->h - s.y;
    }
    if (d.x + s.w > dst->w) {
        s.w = dst->w - d.x;
    }
    if (d.y + s.h > dst->h) {
        s.h = dst->h - d.y;
    }
    if (s.w <= 0 || s.h <= 0) {
        return 0;
    }

    size_t bpp = dst->format ? dst->format->BytesPerPixel : sizeof(uint32_t);
    if (bpp == 0) {
        bpp = sizeof(uint32_t);
    }
    size_t row_bytes = (size_t)s.w * bpp;
    uint8_t *src_base = (uint8_t *)src->pixels + (size_t)s.y * (size_t)src->pitch + (size_t)s.x * bpp;
    uint8_t *dst_base = (uint8_t *)dst->pixels + (size_t)d.y * (size_t)dst->pitch + (size_t)d.x * bpp;

    if (src == dst && dst_base > src_base) {
        for (int y = s.h; y > 0; --y) {
            memmove(dst_base + (size_t)(y - 1) * (size_t)dst->pitch,
                    src_base + (size_t)(y - 1) * (size_t)src->pitch,
                    row_bytes);
        }
    } else {
        for (int y = 0; y < s.h; ++y) {
            memmove(dst_base + (size_t)y * (size_t)dst->pitch,
                    src_base + (size_t)y * (size_t)src->pitch,
                    row_bytes);
        }
    }

    return 0;
}



#define SDL12_FLUSH_INTERVAL_MS 16u  /* ~60 fps cap */

static void flush_dirty_rect(void)
{
    if (!g_dirty_active || g_window == 0) {
        return;
    }
    uint64_t now = get_uptime_ms();
    if (now - g_dirty_last_flush_ms < SDL12_FLUSH_INTERVAL_MS) {
        /* Not yet time for next frame — keep accumulating */
        return;
    }
    if (g_dirty_x2 > g_dirty_x1 && g_dirty_y2 > g_dirty_y1) {
        Uint32 w = (Uint32)(g_dirty_x2 - g_dirty_x1);
        Uint32 h = (Uint32)(g_dirty_y2 - g_dirty_y1);
        window_damage(g_window, (uint32_t)g_dirty_x1, (uint32_t)g_dirty_y1, w, h);
    }
    g_dirty_active = false;
    g_dirty_last_flush_ms = now;
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
    if (!screen || g_window == 0) {
        return;
    }
    if (w == 0 || h == 0) {
        x = 0;
        y = 0;
        w = (Uint32)screen->w;
        h = (Uint32)screen->h;
    }
    if (x < 0) {
        w = (Uint32)((Sint32)w + x);
        x = 0;
    }
    if (y < 0) {
        h = (Uint32)((Sint32)h + y);
        y = 0;
    }
    if ((int)(x + (Sint32)w) > screen->w) {
        w = (Uint32)(screen->w - x);
    }
    if ((int)(y + (Sint32)h) > screen->h) {
        h = (Uint32)(screen->h - y);
    }
    if ((Sint32)w <= 0 || (Sint32)h <= 0) {
        return;
    }
    if (!g_surface_opaque) {
        make_updated_pixels_opaque(screen, x, y, w, h);
    }

    Sint32 x2 = x + (Sint32)w;
    Sint32 y2 = y + (Sint32)h;
    if (!g_dirty_active) {
        g_dirty_x1 = x;
        g_dirty_y1 = y;
        g_dirty_x2 = x2;
        g_dirty_y2 = y2;
        g_dirty_active = true;
    } else {
        if (x < g_dirty_x1) g_dirty_x1 = x;
        if (y < g_dirty_y1) g_dirty_y1 = y;
        if (x2 > g_dirty_x2) g_dirty_x2 = x2;
        if (y2 > g_dirty_y2) g_dirty_y2 = y2;
    }
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors,
                  int firstcolor, int ncolors)
{
    (void)surface;
    (void)colors;
    (void)firstcolor;
    (void)ncolors;
    return 1;
}

int SDL_ShowCursor(int toggle)
{
    (void)toggle;
    return SDL_DISABLE;
}

int SDL_EnableKeyRepeat(int delay, int interval)
{
    (void)delay;
    (void)interval;
    return 0;
}

int SDL_PollEvent(SDL_Event *event)
{
    flush_dirty_rect();
    process_timers();
    pump_window_events();
    return queue_pop(event);
}

int SDL_WaitEvent(SDL_Event *event)
{
    for (;;) {
        flush_dirty_rect();
        process_timers();
        pump_window_events();
        if (queue_pop(event)) {
            return 1;
        }

        sleep_ms(1u);
    }
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback,
                         void *param)
{
    if (!callback) {
        sdl12_set_error("SDL_AddTimer: null callback");
        return 0;
    }
    for (size_t i = 0; i < SDL12_MAX_TIMERS; ++i) {
        if (g_timers[i].active) {
            continue;
        }
        SDL_TimerID id = g_next_timer_id++;
        if (id == 0) {
            id = g_next_timer_id++;
        }
        g_timers[i].active = true;
        g_timers[i].id = id;
        g_timers[i].interval = interval;
        g_timers[i].deadline_ms = get_uptime_ms() + interval;
        g_timers[i].callback = callback;
        g_timers[i].param = param;
        return id;
    }
    sdl12_set_error("SDL timer table full");
    return 0;
}

int SDL_RemoveTimer(SDL_TimerID id)
{
    for (size_t i = 0; i < SDL12_MAX_TIMERS; ++i) {
        if (g_timers[i].active && g_timers[i].id == id) {
            g_timers[i].active = false;
            return 1;
        }
    }
    return 0;
}
