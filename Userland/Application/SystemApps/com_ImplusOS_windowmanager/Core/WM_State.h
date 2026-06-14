#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../../../Userland/API/IPC.h"
#include "../../../../../Userland/API/Input.h"
#include "../../../../../Userland/API/WM_Protocol.h"

#define WM_MAX_WINDOWS                 256u
#define WM_MAX_INPUT_SUBSCRIPTIONS     128u
#define WM_MAX_NOTIFICATIONS           6u
#define WM_MAX_NOTIFICATION_HISTORY    16u
#define WM_MAX_LAUNCHER_APPS           32u
#define WM_MAX_DAMAGE_RECTS            32u
#define WM_EVENT_QUEUE_SIZE            128u
#define WM_TITLE_MAX                   64u
#define WM_APP_NAME_MAX                40u
#define WM_APP_PATH_MAX                192u
#define WM_THEME_PATH_MAX              192u
#define WM_XML_MAX_BYTES               (1024u * 1024u)
#define WM_SURFACE_MAX_BYTES           (64u * 1024u * 1024u)

#define WM_TASKBAR_AT_TOP              0

#define WM_TITLE_HEIGHT                36u
#define WM_CORNER_RADIUS               12u
#define WM_SHADOW_SIZE                 10u
#define WM_DOCK_HEIGHT                 25u
#define WM_DOCK_MARGIN                 0u
#define WM_TITLE_BUTTON_WIDTH          40u
#define WM_MIN_WINDOW_WIDTH            160u
#define WM_MIN_WINDOW_HEIGHT           80u
#define WM_CURSOR_WIDTH                14u
#define WM_CURSOR_HEIGHT               22u

#define WM_SEARCH_MAX                  128u

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
} wm_rect_t;

typedef struct {
    wm_rect_t rects[WM_MAX_DAMAGE_RECTS];
    uint32_t count;
    bool full;
} wm_region_t;

typedef enum {
    WM_LAYER_DESKTOP = 0,
    WM_LAYER_NORMAL,
    WM_LAYER_FLOATING,
    WM_LAYER_PANEL,
    WM_LAYER_OVERLAY,
    WM_LAYER_CURSOR,
    WM_LAYER_COUNT
} wm_layer_t;

typedef enum {
    WM_HIT_NONE = 0,
    WM_HIT_CONTENT,
    WM_HIT_TITLE,
    WM_HIT_CLOSE,
    WM_HIT_MAXIMIZE,
    WM_HIT_MINIMIZE,
    WM_HIT_RESIZE_LEFT,
    WM_HIT_RESIZE_RIGHT,
    WM_HIT_RESIZE_TOP,
    WM_HIT_RESIZE_BOTTOM,
    WM_HIT_RESIZE_TOP_LEFT,
    WM_HIT_RESIZE_TOP_RIGHT,
    WM_HIT_RESIZE_BOTTOM_LEFT,
    WM_HIT_RESIZE_BOTTOM_RIGHT
} wm_hit_zone_t;

typedef enum {
    WM_TRANSITION_NONE = 0,
    WM_TRANSITION_SHOW,
    WM_TRANSITION_CLOSE,
    WM_TRANSITION_HIDE,
    WM_TRANSITION_MINIMIZE,
    WM_TRANSITION_RESTORE
} wm_transition_t;

typedef enum {
    WM_CURSOR_DEFAULT = 0,
    WM_CURSOR_RESIZE_HORIZONTAL,
    WM_CURSOR_RESIZE_VERTICAL,
    WM_CURSOR_RESIZE_DIAGONAL_NW_SE,
    WM_CURSOR_RESIZE_DIAGONAL_NE_SW
} wm_cursor_style_t;

typedef struct {
    uint32_t bg_top;
    uint32_t bg_mid;
    uint32_t bg_bottom;
    uint32_t bg_glow;
    uint32_t surface;
    uint32_t surface_alt;
    uint32_t surface_hover;
    uint32_t title_active;
    uint32_t title_inactive;
    uint32_t border;
    uint32_t border_focus;
    uint32_t accent;
    uint32_t accent_alt;
    uint32_t accent_soft;
    uint32_t text;
    uint32_t text_dim;
    uint32_t danger;
    uint32_t shadow;
    uint32_t dock;
    uint32_t dock_border;
    uint32_t notification;
    uint32_t title_height;
    uint32_t corner_radius;
    uint32_t shadow_size;
    uint32_t dock_height;
    float font_normal;
    float font_small;
    float font_title;
} wm_theme_t;

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    wm_rect_t clip;
} wm_canvas_t;

typedef struct {
    void *impl;
    bool loaded;
} wm_font_t;

typedef struct wm_window {
    uint32_t id;
    int32_t owner_pid;
    wm_rect_t frame;
    wm_rect_t restore_frame;
    uint32_t bg_color;
    char title[WM_TITLE_MAX];

    bool visible;
    bool has_focus;
    bool is_system;
    bool minimized;
    bool maximized;
    bool close_requested;
    bool hover_close;
    bool hover_maximize;
    bool hover_minimize;

    wm_layer_t layer;
    struct wm_window *z_prev;
    struct wm_window *z_next;

    uint32_t *surface;
    uint32_t surface_bytes;
    int32_t surface_shared_memory_handle;
    wm_region_t damage;
    uint32_t transaction_depth;
    bool transaction_dirty;

    char *xml_buffer;
    uint32_t xml_size;
    uint32_t xml_capacity;

    uint32_t icon[32u * 32u];
    bool has_icon;

    uint8_t *shadow_mask;
    uint32_t shadow_mask_width;
    uint32_t shadow_mask_height;
    uint32_t shadow_cache_frame_width;
    uint32_t shadow_cache_frame_height;
    uint32_t shadow_cache_radius;
    uint32_t shadow_cache_size;
    uint8_t shadow_cache_alpha;

    wm_transition_t transition;
    uint64_t transition_started_ms;
    uint32_t transition_duration_ms;
    float visual_alpha;
    float visual_scale;
    float visual_offset_y;
} wm_window_t;

typedef struct {
    int32_t subscriber_pid;
    uint32_t window_id;
    uint32_t input_types;
} wm_input_subscription_t;

typedef struct {
    wm_window_t *id_table[WM_MAX_WINDOWS + 1u];
    wm_window_t *layer_top[WM_LAYER_COUNT];
    wm_window_t *layer_bottom[WM_LAYER_COUNT];
    uint32_t window_count;
    uint32_t focused_id;
    wm_input_subscription_t input_subs[WM_MAX_INPUT_SUBSCRIPTIONS];
    uint32_t input_sub_count;
    uint32_t cursor_x;
    uint32_t cursor_y;
    bool cursor_visible;
    wm_cursor_style_t cursor_style;
} wm_scene_t;

typedef struct {
    uint32_t *shadow;
    uint32_t *background;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t buffer_bytes;
    wm_region_t damage;
    uint32_t previous_cursor_x;
    uint32_t previous_cursor_y;
    bool previous_cursor_visible;
    wm_cursor_style_t previous_cursor_style;
    uint64_t next_frame_ms;
} wm_compositor_t;

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
} wm_icon_image_t;

typedef struct {
    wm_icon_image_t launcher;
    wm_icon_image_t power;
    wm_icon_image_t reboot;
    wm_icon_image_t notification;
    wm_icon_image_t network;
    wm_icon_image_t volume;
    wm_icon_image_t battery;
    wm_icon_image_t close;
    wm_icon_image_t maximize;
    wm_icon_image_t restore;
    wm_icon_image_t minimize;
    wm_icon_image_t window;
    wm_icon_image_t application;
    wm_icon_image_t search;
} wm_system_icons_t;

typedef struct {
    char name[WM_APP_NAME_MAX];
    char path[WM_APP_PATH_MAX];
    char badge[4];
    uint32_t *icon_pixels;
    uint32_t icon_width;
    uint32_t icon_height;
} wm_launcher_app_t;

typedef struct {
    wm_launcher_app_t apps[WM_MAX_LAUNCHER_APPS];
    uint32_t app_count;
    uint32_t *wallpaper_pixels;
    uint32_t wallpaper_width;
    uint32_t wallpaper_height;
    wm_system_icons_t system_icons;
} wm_assets_t;

typedef struct {
    char title[64];
    char message[128];
    uint64_t start_ms;
    uint32_t duration_ms;
    bool active;
} wm_notification_t;

typedef struct {
    char title[64];
    char message[128];
    uint64_t created_ms;
    bool valid;
} wm_notification_history_t;

typedef struct {
    ipc_message_t messages[WM_EVENT_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t dropped;
} wm_event_queue_t;

typedef struct {
    bool dragging;
    bool resizing;
    uint32_t active_window_id;
    wm_hit_zone_t resize_zone;
    int32_t pointer_start_x;
    int32_t pointer_start_y;
    wm_rect_t frame_start;
    uint8_t previous_buttons;
    uint64_t last_title_click_ms;
    uint32_t last_title_click_window;
} wm_input_state_t;

typedef struct {
    wm_scene_t scene;
    wm_compositor_t compositor;
    wm_theme_t theme;
    wm_font_t font;
    wm_assets_t assets;
    wm_event_queue_t event_queue;
    wm_input_state_t input;
    wm_notification_t notifications[WM_MAX_NOTIFICATIONS];
    wm_notification_history_t notification_history[WM_MAX_NOTIFICATION_HISTORY];
    uint32_t notification_history_count;
    uint32_t notification_history_next;
    uint32_t notification_unread_count;
    uint32_t notification_center_scroll;
    bool notification_center_open;
    bool launcher_open;
    uint32_t launcher_scroll;
    int32_t launcher_hover_index;
    uint32_t taskbar_hover_kind;
    uint32_t taskbar_hover_window_id;
    char clock_text[32];
    char date_text[16];
    char search_text[WM_SEARCH_MAX];
    uint32_t search_len;
    bool search_active;
    bool running;
} wm_state_t;

extern wm_state_t g_wm_state;

static inline int32_t wm_max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t wm_min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static inline uint32_t wm_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t wm_max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }