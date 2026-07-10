#include "WM_Taskbar.h"

#include "../Compositor/WM_Raster.h"
#include "../Font/WM_Font.h"
#include "../../../../../Userland/API/Time.h"

#include <stdio.h>
#include <string.h>

static const char *month_names[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char *day_names[] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};

#define TB_LAUNCHER_W   48u
#define TB_TRAY_W       80u
#define TB_NOTIF_W      36u
#define TB_CLOCK_W      128u
#define TB_RIGHT_TOTAL  (TB_TRAY_W + TB_NOTIF_W + TB_CLOCK_W + 8u)
#define TB_SEP_W        1u

/* Simple day-of-week Tomohiko Sakamoto algorithm */
static uint32_t day_of_week(uint32_t y, uint32_t m, uint32_t d)
{
    static const uint32_t t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    return (y + y/4u - y/100u + y/400u + t[m-1u] + d) % 7u;
}

bool wm_taskbar_update_clock(wm_state_t *state)
{
    if (!state) return false;
    rtc_time_t t;
    if (sys_get_rtc_time(&t) < 0) return false;

    char next_clock[sizeof(state->clock_text)] = {0};
    char next_date[sizeof(state->date_text)] = {0};
    snprintf(next_clock, sizeof(next_clock),
             "%02u:%02u:%02u",
             (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
    uint32_t dow = day_of_week(t.year, t.month, t.day);
    uint32_t mon_idx = t.month >= 1u && t.month <= 12u ? t.month - 1u : 0u;
    snprintf(next_date, sizeof(next_date),
             "%s %u %s",
             day_names[dow % 7u], (unsigned)t.day, month_names[mon_idx]);

    if (strcmp(state->clock_text, next_clock) == 0 &&
        strcmp(state->date_text, next_date) == 0) return false;
    memcpy(state->clock_text, next_clock, sizeof(state->clock_text));
    memcpy(state->date_text, next_date, sizeof(state->date_text));
    return true;
}

wm_rect_t wm_taskbar_rect(const wm_state_t *state)
{
    if (!state || state->compositor.framebuffer_width == 0u)
        return (wm_rect_t){0, 0, 0, 0};
    uint32_t height = state->theme.dock_height;
    if (height > state->compositor.framebuffer_height)
        height = state->compositor.framebuffer_height;
    return (wm_rect_t){
        0,
#if WM_TASKBAR_AT_TOP
        0,
#else
        (int32_t)(state->compositor.framebuffer_height - height),
#endif
        state->compositor.framebuffer_width,
        height
    };
}

wm_rect_t wm_taskbar_clock_rect(const wm_state_t *state)
{
    wm_rect_t dock = wm_taskbar_rect(state);
    if (dock.w == 0u || dock.h == 0u || dock.w <= TB_RIGHT_TOTAL) {
        return (wm_rect_t){0, 0, 0, 0};
    }

    int32_t notif_x = (int32_t)dock.w - (int32_t)TB_RIGHT_TOTAL - 4 + 8 +
                      (int32_t)TB_TRAY_W;
    int32_t clock_x = notif_x + (int32_t)TB_NOTIF_W;
    int32_t x = clock_x - 2;
    uint32_t w = TB_CLOCK_W + 4u;
    if (x < dock.x) {
        uint32_t trim = (uint32_t)(dock.x - x);
        x = dock.x;
        w = w > trim ? w - trim : 0u;
    }
    if ((int64_t)x + (int64_t)w > (int64_t)dock.x + (int64_t)dock.w) {
        w = (uint32_t)((int64_t)dock.x + (int64_t)dock.w - (int64_t)x);
    }
    return (wm_rect_t){x, dock.y, w, dock.h};
}

static bool cursor_in(const wm_state_t *state, wm_rect_t r)
{
    int32_t cx = (int32_t)state->scene.cursor_x;
    int32_t cy = (int32_t)state->scene.cursor_y;
    return cx >= r.x && cx < r.x + (int32_t)r.w &&
           cy >= r.y && cy < r.y + (int32_t)r.h;
}

static uint32_t apply_opacity(uint32_t color, uint8_t opacity)
{
    uint32_t alpha = ((color >> 24u) * (uint32_t)opacity) / 255u;
    return (color & 0x00FFFFFFu) | (alpha << 24u);
}

static uint32_t visible_window_count(const wm_state_t *state)
{
    uint32_t count = 0u;
    for (uint32_t id = 1u; id <= WM_MAX_WINDOWS; ++id) {
        const wm_window_t *w = state->scene.id_table[id];
        if (w && w->visible && !w->is_system && !w->close_requested) ++count;
    }
    return count;
}

static void task_layout(const wm_state_t *state, wm_rect_t dock,
                        uint32_t *button_width, uint32_t *slot_count,
                        int32_t *start_x)
{
    uint32_t count = visible_window_count(state);
    int32_t tasks_left  = (int32_t)TB_LAUNCHER_W + 4;
    int32_t tasks_right = (int32_t)dock.w - (int32_t)TB_RIGHT_TOTAL - 8;
    uint32_t available  = tasks_right > tasks_left ?
        (uint32_t)(tasks_right - tasks_left) : 0u;
    uint32_t width = (count == 0u) ? 0u : available / count;
    uint32_t slots = count;
    if (count != 0u && width == 0u) { width = 1u; slots = available; }
    if (width > 200u) width = 200u;
    *button_width = width;
    *slot_count   = slots;
    *start_x      = tasks_left;
}

static void draw_icon_btn(wm_state_t *state, wm_canvas_t *canvas,
                          wm_rect_t btn, const wm_icon_image_t *icon,
                          bool active, bool hover)
{
    uint32_t bg = active  ? state->theme.accent_soft :
                  hover   ? state->theme.surface_hover : 0u;
    if (bg)
        wm_canvas_fill_rounded(canvas, btn, btn.h / 2u, bg);
    if (icon) {
        uint32_t isz = 18u;
        wm_canvas_draw_icon(canvas,
            (wm_rect_t){btn.x + (int32_t)(btn.w - isz) / 2,
                        btn.y + (int32_t)(btn.h - isz) / 2,
                        isz, isz},
            icon, 220u, 2u, state->theme.text);
    }
}

void wm_taskbar_draw(wm_state_t *state, wm_canvas_t *canvas)
{
    if (!state || !canvas) return;
    wm_rect_t dock = wm_taskbar_rect(state);
    if (dock.w == 0u) return;
    if (!wm_rect_intersects(dock, canvas->clip)) return;

    wm_canvas_fill(canvas, dock, state->theme.dock);
    wm_canvas_fill(canvas,
        (wm_rect_t){0,
#if WM_TASKBAR_AT_TOP
                      dock.y + (int32_t)dock.h - 1,
#else
                      dock.y,
#endif
                      dock.w, 1u},
        state->theme.border);

    int32_t cy = dock.y + (int32_t)dock.h / 2;
    int32_t pad = 4;

    wm_rect_t launcher = {pad, dock.y + pad, TB_LAUNCHER_W - pad * 2, dock.h - pad * 2};
    bool launcher_hover = cursor_in(state, launcher);
    uint32_t launcher_bg = state->launcher_open ? state->theme.accent_soft :
                           launcher_hover        ? state->theme.surface_hover : 0u;
    if (launcher_bg)
        wm_canvas_fill_rounded(canvas, launcher, 8u, launcher_bg);
    {
        wm_icon_image_t *logo = &state->assets.system_icons.logo;
        uint32_t isz = 24u;
        int32_t ix = launcher.x + (int32_t)(launcher.w - isz) / 2;
        int32_t iy = launcher.y + (int32_t)(launcher.h - isz) / 2;
        if (logo->pixels && logo->width > 0 && logo->height > 0) {
            wm_canvas_blit_scaled(canvas, (wm_rect_t){ix, iy, isz, isz},
                                  logo->pixels, logo->width, logo->height, 255u, 2u);
        } else {
            uint32_t rect_color = state->launcher_open ? state->theme.accent : state->theme.text;
            wm_canvas_fill_rounded(canvas, (wm_rect_t){ix, iy, isz, isz}, 4u,
                                   rect_color | 0xFF000000u);
        }
        (void)cy;
    }

    uint32_t btn_w = 0u, slots = 0u;
    int32_t btn_x = 0;
    task_layout(state, dock, &btn_w, &slots, &btn_x);
    uint32_t drawn = 0u;
    for (uint32_t id = 1u; id <= WM_MAX_WINDOWS && btn_w != 0u; ++id) {
        wm_window_t *win = state->scene.id_table[id];
        if (!win || !win->visible || win->is_system || win->close_requested) continue;
        if (drawn >= slots) break;
        uint32_t inset = btn_w >= 10u ? 3u : 0u;
        wm_rect_t btn = {btn_x + (int32_t)inset, dock.y + pad,
                         btn_w > inset * 2u ? btn_w - inset * 2u : btn_w,
                         dock.h - pad * 2};
        bool hover = cursor_in(state, btn);
        uint32_t bg = win->has_focus ? state->theme.accent_soft :
                      hover           ? state->theme.surface_hover : 0u;
        if (bg) wm_canvas_fill_rounded(canvas, btn, 7u, bg);

        if (btn.w >= 32u) {
            uint32_t isz = 22u;
            int32_t ix = btn.x + (btn.w >= 120u ? 8 : (int32_t)(btn.w - isz) / 2);
            int32_t iy = btn.y + (int32_t)(btn.h - isz) / 2;
            if (win->has_icon)
                wm_canvas_blit_scaled(canvas, (wm_rect_t){ix, iy, isz, isz},
                                      win->icon, 32u, 32u, 255u, 4u);
            else
                wm_canvas_draw_icon(canvas, (wm_rect_t){ix, iy, isz, isz},
                                    &state->assets.system_icons.window,
                                    255u, 4u, state->theme.text);
        }
        if (btn.w >= 100u) {
            const char *title = win->title[0] ? win->title : "Window";
            wm_font_draw(&state->font, canvas,
                         btn.x + 36, btn.y + (int32_t)(btn.h - 13u) / 2,
                         title,
                         win->has_focus ? state->theme.text : state->theme.text_dim,
                         state->theme.font_small,
                         btn.w > 44u ? btn.w - 44u : 0u);
        }
        if (win->has_focus || win->minimized) {
            uint32_t ind_w = win->has_focus ? 24u : 6u;
            ind_w = wm_min_u32(ind_w, btn.w);
            wm_canvas_fill_rounded(canvas,
                (wm_rect_t){btn.x + (int32_t)(btn.w - ind_w) / 2,
                            btn.y + (int32_t)btn.h - 3, ind_w, 3u},
                2u, win->has_focus ? state->theme.accent : state->theme.text_dim);
        }
        btn_x += (int32_t)btn_w;
        ++drawn;
    }
    
    int32_t rx = (int32_t)dock.w - (int32_t)TB_RIGHT_TOTAL - 4;
    wm_canvas_fill(canvas,
        (wm_rect_t){rx, dock.y + pad + 4, 1u, (uint32_t)dock.h - (uint32_t)pad * 2u - 8u},
        state->theme.border);
    rx += 8;

    {
        uint32_t isz = 16u;
        int32_t ty = dock.y + (int32_t)dock.h / 2 - (int32_t)isz / 2;
        wm_canvas_draw_icon(canvas, (wm_rect_t){rx, ty, isz, isz},
            &state->assets.system_icons.network, 200u, 2u, state->theme.text_dim);
        wm_canvas_draw_icon(canvas, (wm_rect_t){rx + 22, ty, isz, isz},
            &state->assets.system_icons.volume, 200u, 2u, state->theme.text_dim);
        wm_canvas_draw_icon(canvas, (wm_rect_t){rx + 44, ty, isz, isz},
            &state->assets.system_icons.battery, 200u, 2u, state->theme.text_dim);
        rx += (int32_t)TB_TRAY_W;
    }

    {
        wm_rect_t nbtn = {rx, dock.y + pad, TB_NOTIF_W - 4u, dock.h - pad * 2};
        bool nhover = cursor_in(state, nbtn);
        draw_icon_btn(state, canvas, nbtn,
                      &state->assets.system_icons.notification,
                      state->notification_center_open, nhover);
        if (state->notification_unread_count) {
            wm_canvas_fill_rounded(canvas,
                (wm_rect_t){nbtn.x + (int32_t)nbtn.w - 10,
                             nbtn.y + 4, 8u, 8u},
                4u, state->theme.accent | 0xFF000000u);
        }
        rx += (int32_t)TB_NOTIF_W;
    }

    if (state->clock_text[0]) {
        int32_t text_y_top = dock.y + 7;
        int32_t text_y_bot = dock.y + (int32_t)dock.h / 2 + 2;
        wm_font_draw(&state->font, canvas, rx + 2, text_y_top,
                     state->clock_text, state->theme.text, 12.0f, TB_CLOCK_W);
        if (state->date_text[0])
            wm_font_draw(&state->font, canvas, rx + 2, text_y_bot,
                         state->date_text, state->theme.text_dim, 10.0f, TB_CLOCK_W);
    }
}

wm_taskbar_hit_t wm_taskbar_hit_test(wm_state_t *state, int32_t x, int32_t y)
{
    wm_taskbar_hit_t result = {WM_TASKBAR_HIT_NONE, 0u};
    if (!state) return result;
    wm_rect_t dock = wm_taskbar_rect(state);
    if (!wm_rect_intersects((wm_rect_t){x, y, 1u, 1u}, dock)) return result;

    if (x >= 4 && x < 4 + (int32_t)TB_LAUNCHER_W) {
        result.kind = WM_TASKBAR_HIT_LAUNCHER;
        return result;
    }
    int32_t notif_x = (int32_t)dock.w - (int32_t)TB_RIGHT_TOTAL - 4 + 8 +
                      (int32_t)TB_TRAY_W;
    int32_t clock_x = notif_x + (int32_t)TB_NOTIF_W;
    if (x >= notif_x && x < notif_x + (int32_t)TB_NOTIF_W) {
        result.kind = WM_TASKBAR_HIT_NOTIFICATION;
        return result;
    }
    if (x >= clock_x) {
        result.kind = WM_TASKBAR_HIT_CLOCK;
        return result;
    }

    uint32_t btn_w = 0u, slots = 0u;
    int32_t btn_x = 0;
    task_layout(state, dock, &btn_w, &slots, &btn_x);
    uint32_t tested = 0u;
    for (uint32_t id = 1u; id <= WM_MAX_WINDOWS && btn_w != 0u; ++id) {
        wm_window_t *win = state->scene.id_table[id];
        if (!win || !win->visible || win->is_system || win->close_requested) continue;
        if (tested >= slots) break;
        if (x >= btn_x && x < btn_x + (int32_t)btn_w) {
            result.kind = WM_TASKBAR_HIT_WINDOW;
            result.window_id = id;
            return result;
        }
        btn_x += (int32_t)btn_w;
        ++tested;
    }
    return result;
}
