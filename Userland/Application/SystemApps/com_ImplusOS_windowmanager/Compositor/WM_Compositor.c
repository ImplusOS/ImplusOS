#include "WM_Compositor.h"

#include "WM_Damage.h"
#include "WM_Raster.h"
#include "../Decoration/WM_Decoration.h"
#include "../UI/WM_Notification.h"
#include "../UI/WM_StartMenu.h"
#include "../UI/WM_Taskbar.h"
#include "../../../../../Userland/Syscalls.h"

#include <stdlib.h>
#include <string.h>

static wm_rect_t display_bounds(const wm_state_t *state)
{
    return (wm_rect_t){0, 0, state->compositor.framebuffer_width,
                       state->compositor.framebuffer_height};
}

static void flush_rect(wm_state_t *state, wm_rect_t rect);

bool wm_compositor_init(wm_state_t *state, uint32_t width, uint32_t height)
{
    if (!state || !width || !height) return false;
    uint64_t bytes64 = (uint64_t)width * height * sizeof(uint32_t);
    if (bytes64 > SIZE_MAX || bytes64 > UINT32_MAX) return false;
    wm_compositor_t *c = &state->compositor;
    memset(c, 0, sizeof(*c));
    c->shadow     = (uint32_t *)malloc((size_t)bytes64);
    c->background = (uint32_t *)malloc((size_t)bytes64);
    if (!c->shadow || !c->background) { wm_compositor_destroy(c); return false; }
    c->framebuffer_width  = width;
    c->framebuffer_height = height;
    c->buffer_bytes       = (uint32_t)bytes64;
    wm_region_reset(&c->damage);
    return true;
}

void wm_compositor_destroy(wm_compositor_t *compositor)
{
    if (!compositor) return;
    free(compositor->shadow);
    free(compositor->background);
    memset(compositor, 0, sizeof(*compositor));
}

void wm_compositor_generate_background(wm_state_t *state)
{
    if (!state || !state->compositor.background) return;
    wm_canvas_t canvas;
    wm_canvas_init(&canvas, state->compositor.background,
                   state->compositor.framebuffer_width,
                   state->compositor.framebuffer_height,
                   state->compositor.framebuffer_width);
    wm_canvas_fill(&canvas, display_bounds(state), state->theme.bg_bottom);
    if (state->assets.wallpaper_pixels) {
        wm_canvas_blit_scaled(&canvas, display_bounds(state),
            state->assets.wallpaper_pixels,
            state->assets.wallpaper_width,
            state->assets.wallpaper_height, 255u, 0u);
    }
}

void wm_compositor_damage_all(wm_state_t *state)
{
    if (state) wm_region_add_full(&state->compositor.damage);
}

bool wm_compositor_has_pending_frame(const wm_state_t *state)
{
    if (!state) return false;
    const wm_compositor_t *c = &state->compositor;
    if (!wm_region_is_empty(&c->damage)) return true;
    return c->previous_cursor_visible != state->scene.cursor_visible ||
           c->previous_cursor_x       != state->scene.cursor_x       ||
           c->previous_cursor_y       != state->scene.cursor_y       ||
           c->previous_cursor_style   != state->scene.cursor_style;
}

static void copy_background(wm_state_t *state, wm_rect_t rect)
{
    uint32_t width = state->compositor.framebuffer_width;
    for (uint32_t row = 0; row < rect.h; ++row) {
        uint32_t offset = (uint32_t)(rect.y+(int32_t)row)*width+(uint32_t)rect.x;
        memcpy(&state->compositor.shadow[offset],
               &state->compositor.background[offset],
               (size_t)rect.w * sizeof(uint32_t));
    }
}

static void draw_scene(wm_state_t *state, wm_canvas_t *canvas)
{
    for (uint32_t layer = WM_LAYER_NORMAL; layer <= WM_LAYER_OVERLAY; ++layer)
        for (wm_window_t *w = state->scene.layer_bottom[layer]; w; w = w->z_prev)
            wm_decoration_draw_window(state, canvas, w);
}

static void draw_default_cursor(wm_state_t *state, wm_canvas_t *canvas,
                                int32_t x, int32_t y)
{
    static const char shape[WM_CURSOR_HEIGHT][WM_CURSOR_WIDTH + 1u] = {
        "B.............", "BB............", "BFB...........",
        "BFFB..........", "BFFFB.........", "BFFFFB........",
        "BFFFFFB.......", "BFFFFFFB......", "BFFFFFFFB.....",
        "BFFFFFFFFB....", "BFFFFFFFFFB...", "BFFFFFFFFFFB..",
        "BFFFFFFBBBBBB.", "BFFFBFFFB.....", "BFFB.BFFB.....",
        "BFB..BFFB.....", "BB....BFFB....", "B.....BFFB....",
        ".......BFFB...", ".......BFFB...", "........BB....",
        "..............",
    };
    for (uint32_t row = 0; row < WM_CURSOR_HEIGHT; ++row)
        for (uint32_t col = 0; col < WM_CURSOR_WIDTH; ++col)
            if (shape[row][col] != '.')
                wm_canvas_put(canvas, x+(int32_t)col+2, y+(int32_t)row+2, 0x33000000u);
    for (uint32_t row = 0; row < WM_CURSOR_HEIGHT; ++row)
        for (uint32_t col = 0; col < WM_CURSOR_WIDTH; ++col) {
            if (shape[row][col] == '.') continue;
            wm_canvas_put(canvas, x+(int32_t)col, y+(int32_t)row,
                shape[row][col]=='F' ? state->theme.text : 0xFF1A1A1Au);
        }
}

static void draw_resize_cursor(wm_state_t *state, wm_canvas_t *canvas,
                               int32_t x, int32_t y)
{
    uint32_t color   = state->theme.text;
    uint32_t outline = 0x99000000u;
    int32_t cx = x+7, cy = y+7;
    bool hz = state->scene.cursor_style == WM_CURSOR_RESIZE_HORIZONTAL;
    bool vt = state->scene.cursor_style == WM_CURSOR_RESIZE_VERTICAL;
    bool nwse = state->scene.cursor_style == WM_CURSOR_RESIZE_DIAGONAL_NW_SE;
    for (int off = -1; off <= 1; ++off) {
        uint32_t lc = off==0 ? color : outline;
        if (hz)   wm_canvas_line(canvas,cx-7,cy+off,cx+7,cy+off,lc);
        else if (vt)   wm_canvas_line(canvas,cx+off,cy-7,cx+off,cy+7,lc);
        else if (nwse) wm_canvas_line(canvas,cx-6+off,cy-6,cx+6+off,cy+6,lc);
        else           wm_canvas_line(canvas,cx+6+off,cy-6,cx-6+off,cy+6,lc);
    }
}

static void draw_cursor(wm_state_t *state, wm_canvas_t *canvas)
{
    if (!state->scene.cursor_visible) return;
    int32_t x = (int32_t)state->scene.cursor_x;
    int32_t y = (int32_t)state->scene.cursor_y;
    if (state->scene.cursor_style == WM_CURSOR_DEFAULT)
        draw_default_cursor(state, canvas, x, y);
    else
        draw_resize_cursor(state, canvas, x, y);
}

static void draw_cursor_layer(wm_state_t *state)
{
    if (!state->scene.cursor_visible) return;
    uint32_t *framebuffer = (uint32_t *)sys_get_display_framebuffer();
    if (framebuffer) {
        wm_canvas_t canvas;
        wm_canvas_init(&canvas, framebuffer,
                       state->compositor.framebuffer_width,
                       state->compositor.framebuffer_height,
                       state->compositor.framebuffer_width);
        draw_cursor(state, &canvas);
        return;
    }
    wm_canvas_t canvas;
    wm_canvas_init(&canvas, state->compositor.shadow,
                   state->compositor.framebuffer_width,
                   state->compositor.framebuffer_height,
                   state->compositor.framebuffer_width);
    draw_cursor(state, &canvas);
    wm_rect_t cr = wm_rect_intersection(
        (wm_rect_t){(int32_t)state->scene.cursor_x-3,
                    (int32_t)state->scene.cursor_y-3,
                    WM_CURSOR_WIDTH+12u, WM_CURSOR_HEIGHT+12u},
        display_bounds(state));
    if (cr.w && cr.h) flush_rect(state, cr);
}

static void flush_rect(wm_state_t *state, wm_rect_t rect)
{
    uint32_t *framebuffer = (uint32_t *)sys_get_display_framebuffer();
    uint32_t width = state->compositor.framebuffer_width;
    if (framebuffer) {
        for (uint32_t row = 0; row < rect.h; ++row) {
            uint32_t off = (uint32_t)(rect.y+(int32_t)row)*width+(uint32_t)rect.x;
            memcpy(&framebuffer[off], &state->compositor.shadow[off],
                   (size_t)rect.w*sizeof(uint32_t));
        }
        return;
    }
    for (uint32_t row = 0; row < rect.h; ++row) {
        uint32_t y   = (uint32_t)rect.y+row;
        uint32_t off = y*width+(uint32_t)rect.x;
        uint32_t col = 0u;
        while (col < rect.w) {
            uint32_t color = state->compositor.shadow[off+col];
            uint32_t run = 1u;
            while (col+run < rect.w && state->compositor.shadow[off+col+run]==color) ++run;
            draw_fill_rect((uint32_t)rect.x+col, y, run, 1u, color);
            col += run;
        }
    }
}

static void add_cursor_damage(wm_state_t *state)
{
    wm_compositor_t *c = &state->compositor;
    wm_rect_t bounds = display_bounds(state);
    bool changed = c->previous_cursor_visible != state->scene.cursor_visible ||
                   c->previous_cursor_x       != state->scene.cursor_x       ||
                   c->previous_cursor_y       != state->scene.cursor_y       ||
                   c->previous_cursor_style   != state->scene.cursor_style;
    if (!changed) return;
    if (c->previous_cursor_visible)
        wm_region_add(&c->damage,
            (wm_rect_t){(int32_t)c->previous_cursor_x-3,
                        (int32_t)c->previous_cursor_y-3,
                        WM_CURSOR_WIDTH+12u, WM_CURSOR_HEIGHT+12u}, bounds);
    if (state->scene.cursor_visible)
        wm_region_add(&c->damage,
            (wm_rect_t){(int32_t)state->scene.cursor_x-3,
                        (int32_t)state->scene.cursor_y-3,
                        WM_CURSOR_WIDTH+12u, WM_CURSOR_HEIGHT+12u}, bounds);
}

void wm_compositor_render(wm_state_t *state, uint64_t now_ms)
{
    if (!state || !state->compositor.shadow || !state->compositor.background) return;
    add_cursor_damage(state);
    wm_region_t damage = state->compositor.damage;
    if (wm_region_is_empty(&damage)) return;
    wm_region_reset(&state->compositor.damage);

    wm_rect_t full = display_bounds(state);
    wm_rect_t rects[WM_MAX_DAMAGE_RECTS];
    uint32_t rect_count = damage.full ? 1u : damage.count;
    if (damage.full) rects[0] = full;
    else memcpy(rects, damage.rects, sizeof(wm_rect_t)*rect_count);

    for (uint32_t i = 0; i < rect_count; ++i) {
        wm_rect_t rect = wm_rect_intersection(rects[i], full);
        if (!rect.w || !rect.h) continue;
        copy_background(state, rect);
        wm_canvas_t canvas;
        wm_canvas_init(&canvas, state->compositor.shadow,
                       state->compositor.framebuffer_width,
                       state->compositor.framebuffer_height,
                       state->compositor.framebuffer_width);
        wm_canvas_set_clip(&canvas, rect);
        draw_scene(state, &canvas);
        wm_taskbar_draw(state, &canvas);
        wm_start_menu_draw(state, &canvas);
        wm_notification_draw(state, &canvas, now_ms);
        flush_rect(state, rect);
    }
    draw_cursor_layer(state);
    draw_present();
    state->compositor.previous_cursor_x       = state->scene.cursor_x;
    state->compositor.previous_cursor_y       = state->scene.cursor_y;
    state->compositor.previous_cursor_visible = state->scene.cursor_visible;
    state->compositor.previous_cursor_style   = state->scene.cursor_style;
    for (uint32_t id = 1u; id <= WM_MAX_WINDOWS; ++id) {
        wm_window_t *w = state->scene.id_table[id];
        if (w) wm_region_reset(&w->damage);
    }
}