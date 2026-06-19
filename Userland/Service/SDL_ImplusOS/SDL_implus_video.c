#include "SDL_internal.h"

#include "video/SDL_sysvideo.h"
#include "events/SDL_keyboard_c.h"
#include "events/SDL_mouse_c.h"
#include "events/SDL_windowevents_c.h"

#include "Userland/API/Graphics.h"
#include "Userland/API/Input.h"
#include "Userland/API/Process.h"
#include "Userland/API/Window.h"

#define IMPLUS_DRIVER_NAME "ImplusOS"

typedef struct SDL_VideoData {
    uint8_t mouse_buttons;
} SDL_VideoData;

typedef struct SDL_WindowData {
    window_id_t id;
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;
} SDL_WindowData;

window_id_t SDL_ImplusOS_GetWindowID(SDL_Window *window)
{
    SDL_WindowData *data = window ? window->internal : NULL;
    return data ? data->id : 0u;
}

static SDL_Window *implus_find_window(window_id_t id)
{
    SDL_VideoDevice *device = SDL_GetVideoDevice();
    for (SDL_Window *window = device ? device->windows : NULL; window; window = window->next) {
        SDL_WindowData *data = window->internal;
        if (data && data->id == id) {
            return window;
        }
    }
    return device ? device->windows : NULL;
}

static SDL_Scancode implus_translate_keycode(uint16_t keycode)
{
    switch (keycode) {
    case 0x01: return SDL_SCANCODE_ESCAPE;
    case 0x02: return SDL_SCANCODE_1;
    case 0x03: return SDL_SCANCODE_2;
    case 0x04: return SDL_SCANCODE_3;
    case 0x05: return SDL_SCANCODE_4;
    case 0x06: return SDL_SCANCODE_5;
    case 0x07: return SDL_SCANCODE_6;
    case 0x08: return SDL_SCANCODE_7;
    case 0x09: return SDL_SCANCODE_8;
    case 0x0A: return SDL_SCANCODE_9;
    case 0x0B: return SDL_SCANCODE_0;
    case 0x0C: return SDL_SCANCODE_MINUS;
    case 0x0D: return SDL_SCANCODE_EQUALS;
    case 0x0E: return SDL_SCANCODE_BACKSPACE;
    case 0x0F: return SDL_SCANCODE_TAB;
    case 0x10: return SDL_SCANCODE_Q;
    case 0x11: return SDL_SCANCODE_W;
    case 0x12: return SDL_SCANCODE_E;
    case 0x13: return SDL_SCANCODE_R;
    case 0x14: return SDL_SCANCODE_T;
    case 0x15: return SDL_SCANCODE_Y;
    case 0x16: return SDL_SCANCODE_U;
    case 0x17: return SDL_SCANCODE_I;
    case 0x18: return SDL_SCANCODE_O;
    case 0x19: return SDL_SCANCODE_P;
    case 0x1A: return SDL_SCANCODE_LEFTBRACKET;
    case 0x1B: return SDL_SCANCODE_RIGHTBRACKET;
    case 0x1C: return SDL_SCANCODE_RETURN;
    case 0x1D: return SDL_SCANCODE_LCTRL;
    case 0x1E: return SDL_SCANCODE_A;
    case 0x1F: return SDL_SCANCODE_S;
    case 0x20: return SDL_SCANCODE_D;
    case 0x21: return SDL_SCANCODE_F;
    case 0x22: return SDL_SCANCODE_G;
    case 0x23: return SDL_SCANCODE_H;
    case 0x24: return SDL_SCANCODE_J;
    case 0x25: return SDL_SCANCODE_K;
    case 0x26: return SDL_SCANCODE_L;
    case 0x27: return SDL_SCANCODE_SEMICOLON;
    case 0x28: return SDL_SCANCODE_APOSTROPHE;
    case 0x29: return SDL_SCANCODE_GRAVE;
    case 0x2A: return SDL_SCANCODE_LSHIFT;
    case 0x2B: return SDL_SCANCODE_BACKSLASH;
    case 0x2C: return SDL_SCANCODE_Z;
    case 0x2D: return SDL_SCANCODE_X;
    case 0x2E: return SDL_SCANCODE_C;
    case 0x2F: return SDL_SCANCODE_V;
    case 0x30: return SDL_SCANCODE_B;
    case 0x31: return SDL_SCANCODE_N;
    case 0x32: return SDL_SCANCODE_M;
    case 0x33: return SDL_SCANCODE_COMMA;
    case 0x34: return SDL_SCANCODE_PERIOD;
    case 0x35: return SDL_SCANCODE_SLASH;
    case 0x36: return SDL_SCANCODE_RSHIFT;
    case 0x37: return SDL_SCANCODE_KP_MULTIPLY;
    case 0x38: return SDL_SCANCODE_LALT;
    case 0x39: return SDL_SCANCODE_SPACE;
    case 0x3A: return SDL_SCANCODE_CAPSLOCK;
    case 0x3B: return SDL_SCANCODE_F1;
    case 0x3C: return SDL_SCANCODE_F2;
    case 0x3D: return SDL_SCANCODE_F3;
    case 0x3E: return SDL_SCANCODE_F4;
    case 0x3F: return SDL_SCANCODE_F5;
    case 0x40: return SDL_SCANCODE_F6;
    case 0x41: return SDL_SCANCODE_F7;
    case 0x42: return SDL_SCANCODE_F8;
    case 0x43: return SDL_SCANCODE_F9;
    case 0x44: return SDL_SCANCODE_F10;
    case 0x47: return SDL_SCANCODE_KP_7;
    case 0x48: return SDL_SCANCODE_KP_8;
    case 0x49: return SDL_SCANCODE_KP_9;
    case 0x4A: return SDL_SCANCODE_KP_MINUS;
    case 0x4B: return SDL_SCANCODE_KP_4;
    case 0x4C: return SDL_SCANCODE_KP_5;
    case 0x4D: return SDL_SCANCODE_KP_6;
    case 0x4E: return SDL_SCANCODE_KP_PLUS;
    case 0x4F: return SDL_SCANCODE_KP_1;
    case 0x50: return SDL_SCANCODE_KP_2;
    case 0x51: return SDL_SCANCODE_KP_3;
    case 0x52: return SDL_SCANCODE_KP_0;
    case 0x53: return SDL_SCANCODE_KP_PERIOD;
    case 0x57: return SDL_SCANCODE_F11;
    case 0x58: return SDL_SCANCODE_F12;
    case 0x5B: return SDL_SCANCODE_LGUI;
    case 0x5C: return SDL_SCANCODE_RGUI;
    case 0xE01C: return SDL_SCANCODE_KP_ENTER;
    case 0xE01D: return SDL_SCANCODE_RCTRL;
    case 0xE035: return SDL_SCANCODE_KP_DIVIDE;
    case 0xE038: return SDL_SCANCODE_RALT;
    case 0xE047: return SDL_SCANCODE_HOME;
    case 0xE048: return SDL_SCANCODE_UP;
    case 0xE049: return SDL_SCANCODE_PAGEUP;
    case 0xE04B: return SDL_SCANCODE_LEFT;
    case 0xE04D: return SDL_SCANCODE_RIGHT;
    case 0xE04F: return SDL_SCANCODE_END;
    case 0xE050: return SDL_SCANCODE_DOWN;
    case 0xE051: return SDL_SCANCODE_PAGEDOWN;
    case 0xE052: return SDL_SCANCODE_INSERT;
    case 0xE053: return SDL_SCANCODE_DELETE;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

static void implus_send_text(uint8_t ascii)
{
    if (ascii >= 0x20 && ascii < 0x7F) {
        char text[2] = { (char)ascii, '\0' };
        SDL_SendKeyboardText(text);
    }
}

static bool implus_set_relative_mouse_mode(bool enabled)
{
    (void)enabled;
    return true;
}

static bool implus_video_init(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = (int)get_display_width();
    mode.h = (int)get_display_height();
    if (mode.w <= 0) {
        mode.w = 1024;
    }
    if (mode.h <= 0) {
        mode.h = 768;
    }

    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }

    SDL_GetMouse()->SetRelativeMouseMode = implus_set_relative_mouse_mode;
    return true;
}

static void implus_video_quit(SDL_VideoDevice *_this)
{
    (void)_this;
}

static bool implus_create_window(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID create_props)
{
    (void)_this;
    (void)create_props;

    SDL_WindowData *data = SDL_calloc(1, sizeof(*data));
    if (!data) {
        return false;
    }

    uint32_t x = window->undefined_x ? 120u : (uint32_t)SDL_max(window->x, 0);
    uint32_t y = window->undefined_y ? 80u : (uint32_t)SDL_max(window->y, 0);
    uint32_t w = window->w > 0 ? (uint32_t)window->w : 640u;
    uint32_t h = window->h > 0 ? (uint32_t)window->h : 480u;
    const char *title = window->title ? window->title : "SDL";
    if (!window->title && create_props) {
        title = SDL_GetStringProperty(create_props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    }

    data->id = window_create_ex(x, y, w, h, 0xFF000000u, title);
    if (data->id == 0) {
        SDL_free(data);
        return SDL_SetError("window_create_ex failed");
    }
    data->width = w;
    data->height = h;
    window->internal = data;

    (void)window_subscribe_keyboard(data->id);
    (void)window_subscribe_mouse(data->id);
    window_show(data->id);
    window_raise(data->id);
    window_set_focus(data->id);
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, (int)w, (int)h);
    return true;
}

static void implus_destroy_window(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (!data) {
        return;
    }
    window_release_backing_store(data->id);
    window_destroy(data->id);
    SDL_free(data);
    window->internal = NULL;
}

static void implus_show_window(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (data) {
        window_show(data->id);
        window_raise(data->id);
        window_set_focus(data->id);
    }
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);
}

static void implus_hide_window(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (data) {
        window_hide(data->id);
    }
}

static bool implus_set_window_position(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (data) {
        window_set_rect(data->id, (uint32_t)SDL_max(window->pending.x, 0),
                        (uint32_t)SDL_max(window->pending.y, 0),
                        data->width, data->height);
    }
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_MOVED, window->pending.x, window->pending.y);
    return true;
}

static void implus_set_window_size(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (data) {
        data->width = (uint32_t)SDL_max(window->pending.w, 1);
        data->height = (uint32_t)SDL_max(window->pending.h, 1);
        data->pixels = NULL;
        window_set_rect(data->id, (uint32_t)SDL_max(window->x, 0),
                        (uint32_t)SDL_max(window->y, 0),
                        data->width, data->height);
        window->surface_valid = false;
    }
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
}

static void implus_get_window_size_pixels(SDL_VideoDevice *_this, SDL_Window *window, int *w, int *h)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (w) {
        *w = data ? (int)data->width : window->w;
    }
    if (h) {
        *h = data ? (int)data->height : window->h;
    }
}

static bool implus_create_window_framebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (!data) {
        return SDL_SetError("missing ImplusOS window data");
    }

    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t *fb = window_get_backing_store(data->id, &w, &h);
    if (!fb || w == 0 || h == 0) {
        return SDL_SetError("window_get_backing_store failed");
    }

    data->width = w;
    data->height = h;
    data->pixels = fb;
    *format = SDL_PIXELFORMAT_ARGB8888;
    *pixels = fb;
    *pitch = (int)(w * sizeof(uint32_t));
    return true;
}

static bool implus_update_window_framebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (!data) {
        return SDL_SetError("missing ImplusOS window data");
    }
    if (!rects || numrects <= 0) {
        window_damage(data->id, 0, 0, data->width, data->height);
        return true;
    }
    for (int i = 0; i < numrects; ++i) {
        const SDL_Rect *r = &rects[i];
        if (r->w <= 0 || r->h <= 0) {
            continue;
        }
        window_damage(data->id, (uint32_t)SDL_max(r->x, 0), (uint32_t)SDL_max(r->y, 0),
                      (uint32_t)r->w, (uint32_t)r->h);
    }
    return true;
}

static void implus_destroy_window_framebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    (void)_this;
    SDL_WindowData *data = window ? window->internal : NULL;
    if (data) {
        window_release_backing_store(data->id);
        data->pixels = NULL;
    }
}

static void implus_pump_keyboard(void)
{
    input_keyboard_event_t event;
    while (window_input_keyboard_poll(&event) > 0) {
        SDL_Window *window = implus_find_window(0);
        SDL_Scancode scancode = implus_translate_keycode(event.keycode);
        if (window) {
            SDL_SetKeyboardFocus(window);
        }
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, (int)event.keycode, scancode, event.pressed != 0);
        }
        if (event.pressed) {
            implus_send_text(event.ascii);
        }
    }
}

static void implus_send_button_delta(SDL_Window *window, uint8_t old_buttons, uint8_t new_buttons, uint8_t mask, Uint8 sdl_button)
{
    bool was_down = (old_buttons & mask) != 0;
    bool is_down = (new_buttons & mask) != 0;
    if (was_down != is_down) {
        SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, sdl_button, is_down);
    }
}

static void implus_pump_mouse(SDL_VideoDevice *_this)
{
    SDL_VideoData *video_data = _this->internal;
    input_mouse_event_t event;
    while (window_input_mouse_poll(&event) > 0) {
        SDL_Window *window = implus_find_window(0);
        if (!window) {
            continue;
        }
        SDL_SetMouseFocus(window);
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, (float)event.x, (float)event.y);
        implus_send_button_delta(window, video_data->mouse_buttons, event.buttons, INPUT_MOUSE_BTN_LEFT, SDL_BUTTON_LEFT);
        implus_send_button_delta(window, video_data->mouse_buttons, event.buttons, INPUT_MOUSE_BTN_RIGHT, SDL_BUTTON_RIGHT);
        implus_send_button_delta(window, video_data->mouse_buttons, event.buttons, INPUT_MOUSE_BTN_MIDDLE, SDL_BUTTON_MIDDLE);
        if (event.wheel != 0) {
            SDL_SendMouseWheel(0, window, SDL_DEFAULT_MOUSE_ID, 0.0f, (float)event.wheel, SDL_MOUSEWHEEL_NORMAL);
        } else {
            uint8_t changed = (uint8_t)(video_data->mouse_buttons ^ event.buttons);
            if (changed & 0x08u) {
                SDL_SendMouseWheel(0, window, SDL_DEFAULT_MOUSE_ID, 0.0f, 1.0f, SDL_MOUSEWHEEL_NORMAL);
            }
            if (changed & 0x10u) {
                SDL_SendMouseWheel(0, window, SDL_DEFAULT_MOUSE_ID, 0.0f, -1.0f, SDL_MOUSEWHEEL_NORMAL);
            }
        }
        video_data->mouse_buttons = event.buttons;
    }
}

static void implus_pump_events(SDL_VideoDevice *_this)
{
    implus_pump_keyboard();
    implus_pump_mouse(_this);
}

static void implus_delete_device(SDL_VideoDevice *device)
{
    if (device) {
        SDL_free(device->internal);
        SDL_free(device);
    }
}

static SDL_VideoDevice *implus_create_device(void)
{
    SDL_VideoDevice *device = SDL_calloc(1, sizeof(*device));
    if (!device) {
        return NULL;
    }
    SDL_VideoData *data = SDL_calloc(1, sizeof(*data));
    if (!data) {
        SDL_free(device);
        return NULL;
    }

    device->internal = data;
    device->VideoInit = implus_video_init;
    device->VideoQuit = implus_video_quit;
    device->CreateSDLWindow = implus_create_window;
    device->DestroyWindow = implus_destroy_window;
    device->ShowWindow = implus_show_window;
    device->HideWindow = implus_hide_window;
    device->SetWindowPosition = implus_set_window_position;
    device->SetWindowSize = implus_set_window_size;
    device->GetWindowSizeInPixels = implus_get_window_size_pixels;
    device->CreateWindowFramebuffer = implus_create_window_framebuffer;
    device->UpdateWindowFramebuffer = implus_update_window_framebuffer;
    device->DestroyWindowFramebuffer = implus_destroy_window_framebuffer;
    device->PumpEvents = implus_pump_events;
    device->free = implus_delete_device;
    return device;
}

VideoBootStrap PRIVATE_bootstrap = {
    IMPLUS_DRIVER_NAME,
    "ImplusOS SDL video driver",
    implus_create_device,
    NULL,
    true
};
