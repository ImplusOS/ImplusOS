#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../../../Userland/API/IPC.h"

#include "../../../../Userland/API/WM_Protocol.h"

#define WM_MAX_WINDOWS              256
#define WM_MAX_INPUT_SUBSCRIPTIONS  128
#define WM_TITLE_MAX                64
#define WM_TITLE_HEIGHT             32
#define WM_CORNER_RADIUS            15
#define WM_TASKBAR_HEIGHT           40
#define WM_TITLE_BTN_W              46
#define WM_TASKBAR_BTN_W            76
#define WM_TASKBAR_BTN_H            28
#define WM_TASKBAR_BTN_GAP          8
#define WM_SHADOW_SIZE              8
#define WM_GLOW_SIZE                2
#define WM_CURSOR_W  14
#define WM_CURSOR_H  22
#define WM_DIRTY_MARGIN_TOP    (WM_TITLE_HEIGHT + WM_SHADOW_SIZE + 10)
#define WM_DIRTY_MARGIN_RIGHT  (WM_SHADOW_SIZE + 10)
#define WM_DIRTY_MARGIN_BOTTOM (WM_SHADOW_SIZE + 10)

#define COLOR_BG_TOP            0xFF061A2A
#define COLOR_BG_MID            0xFF0C3654
#define COLOR_BG_BOT            0xFF1B7B8F


#define COLOR_TITLEBAR_TOP      0xFF162433
#define COLOR_TITLEBAR_BOT      0xFF0E1823
#define COLOR_TITLEBAR_UNFOCUS  0xFF232C35
#define COLOR_TITLEBAR_UNFOCUS_BOT 0xFF181F27
#define COLOR_TITLEBAR_ACCENT   0xFF47B6D6


#define COLOR_ACCENT            0xFF47B6D6
#define COLOR_ACCENT_DARK       0xFF1E6E89
#define COLOR_ACCENT_GLOW       0x6647B6D6


#define COLOR_CLOSE_RED         0xFFC42B1C
#define COLOR_CLOSE_RED_HOVER   0xFFC42B1C
#define COLOR_MAX_GREEN         0xFF47B6D6
#define COLOR_MAX_GREEN_HOVER   0xFF24394A
#define COLOR_MIN_YELLOW        0xFF47B6D6
#define COLOR_MIN_YELLOW_HOVER  0xFF24394A


#define COLOR_WINDOW_BG         0xFF1F1F1F


#define COLOR_BORDER            0xFF43505D
#define COLOR_BORDER_FOCUS      0xFF47B6D6
#define COLOR_BORDER_SUBTLE     0xFF25303A


#define COLOR_TASKBAR_BG        0xDE102130
#define COLOR_TASKBAR_BG_GLASS  0x8A102130
#define COLOR_TASKBAR_BORDER    0xFF2A3E4D
#define COLOR_TASKBAR_HIGHLIGHT 0x28FFFFFF
#define COLOR_TASKBAR_BTN_ACT   0xFF1F3444
#define COLOR_TASKBAR_BTN_IDLE  0xD21B2631
#define COLOR_TASKBAR_BTN_MIN   0xC0162029
#define COLOR_TASKBAR_INDICATOR 0xFF47B6D6


#define COLOR_BTN_ICON          0xFFCCCCCC
#define COLOR_BTN_ICON_FOCUS    0xFFFFFFFF
#define COLOR_TASKBAR_TEXT      0xFFE7F4F7
#define COLOR_TASKBAR_TEXT_DIM  0xFFA4BEC6
#define COLOR_CURSOR_FILL       0xFFFFFFFF
#define COLOR_CURSOR_BORDER     0xFF000000
#define COLOR_CURSOR_SHADOW     0x60000000
#define COLOR_SHADOW_BASE       0x40000000
#define COLOR_TEXT              0xFFFFFFFF
#define COLOR_TEXT_DIM          0xFF888888


#define COLOR_CLOSE_BTN_HOVER_BG 0xFFC42B1C


#define WM_UI_MAX_ELEMENTS     256
#define WM_UI_TEXT_MAX         128
#define WM_UI_TYPE_LABEL        1
#define WM_UI_TYPE_BUTTON       2
#define WM_UI_TYPE_RECT         3
#define WM_UI_TYPE_PANEL        4

typedef struct {
    uint32_t type;
    char     name[32];
    uint32_t x, y, w, h;
    uint32_t color;
    uint32_t bg_color;
    char     text[WM_UI_TEXT_MAX];
    float    font_size;
} wm_ui_element_t;

typedef struct wm_window {
    uint32_t  id;
    int32_t   owner_pid;
    uint32_t  x, y;
    uint32_t  w, h;
    uint32_t  bg_color;
    char      title[WM_TITLE_MAX];
    bool      visible;
    bool      has_focus;
    bool      is_system;
    bool      hover_close;
    bool      hover_max;
    bool      hover_min;
    bool      minimized;
    bool      maximized;
    uint32_t  restore_x, restore_y;
    uint32_t  restore_w, restore_h;
    float     anim_alpha;
    bool      is_closing;

    struct wm_window *z_prev;
    struct wm_window *z_next;
    
    wm_ui_element_t ui_elements[WM_UI_MAX_ELEMENTS];
    uint32_t        ui_element_count;

    char     *xml_buffer;
    uint32_t xml_size;
    uint32_t xml_capacity;
} wm_window_t;

typedef struct {
    int32_t   subscriber_pid;
    uint32_t  window_id;
    uint32_t  input_types;
} wm_input_sub_t;

typedef struct {
    wm_window_t *windows[WM_MAX_WINDOWS];
    uint32_t     window_count;
    uint32_t     next_id;
    wm_window_t *z_top;
    wm_window_t *z_bottom;
    uint32_t     focused_id;
    wm_input_sub_t input_subs[WM_MAX_INPUT_SUBSCRIPTIONS];
    uint32_t       input_sub_count;
    uint32_t       cursor_x, cursor_y;
    bool           cursor_visible;
} wm_server_t;

typedef struct {
    uint32_t x0, y0;
    uint32_t x1, y1;
    bool     dirty;
} wm_dirty_rect_t;

typedef struct {
    uint32_t  *shadow;
    uint32_t   shadow_bytes;
    uint32_t  *background;
    uint32_t   bg_bytes;
    uint32_t  *front;
    uint32_t   fb_width;
    uint32_t   fb_height;
    wm_dirty_rect_t dirty;
    uint32_t   prev_cx, prev_cy;
    bool       prev_cursor_drawn;
} wm_compositor_t;

typedef struct {
    wm_server_t     server;
    wm_compositor_t compositor;
    bool     dragging;
    uint32_t drag_window_id;
    int32_t  drag_offset_x;
    int32_t  drag_offset_y;
    bool     resizing;
    uint32_t resize_window_id;
    uint8_t  resize_edge;
    int32_t  resize_origin_x;
    int32_t  resize_origin_y;
    uint32_t resize_orig_w;
    uint32_t resize_orig_h;
    uint8_t  prev_mouse_buttons;
    uint8_t *font_buffer;
    bool     font_loaded;
} wm_state_t;

void        wm_server_init(wm_server_t *srv);
int32_t     wm_server_create_window(wm_server_t *srv, int32_t owner, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t bg, const char *title);
void        wm_server_destroy_window(wm_server_t *srv, uint32_t id);
wm_window_t *wm_server_find_window(wm_server_t *srv, uint32_t id);
void        wm_server_set_rect(wm_server_t *srv, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void        wm_server_show(wm_server_t *srv, uint32_t id);
void        wm_server_hide(wm_server_t *srv, uint32_t id);
void        wm_server_raise(wm_server_t *srv, uint32_t id);
void        wm_server_lower(wm_server_t *srv, uint32_t id);
void        wm_server_set_focus(wm_server_t *srv, uint32_t id);
void        wm_server_update_cursor(wm_server_t *srv, uint32_t x, uint32_t y);
uint32_t    wm_server_hit_test(wm_server_t *srv, uint32_t x, uint32_t y);
void        wm_server_route_keyboard(wm_server_t *srv, ipc_message_t *msg);
void        wm_server_route_mouse(wm_server_t *srv, ipc_message_t *msg);
void        wm_server_handle_message(wm_state_t *st, ipc_message_t *msg);
bool        wm_compositor_init(wm_compositor_t *comp, uint32_t width, uint32_t height);
void        wm_compositor_mark_dirty(wm_compositor_t *comp, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void        wm_compositor_render(wm_state_t *st);
void        wm_service_init(wm_state_t *st);
void        wm_service_main_loop(void);
