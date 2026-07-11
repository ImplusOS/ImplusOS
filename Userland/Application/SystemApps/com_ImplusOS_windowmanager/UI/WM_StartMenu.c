#include "WM_StartMenu.h"

#include "../Compositor/WM_Raster.h"
#include "../Font/WM_Font.h"
#include "WM_Taskbar.h"
#include "../../../../../Userland/API/Process.h"

#include <string.h>
#include <stdio.h>

static bool str_icontains(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    for (const char *h = haystack; *h; h++) {
        const char *n = needle;
        const char *p = h;
        while (*n && *p) {
            char ch = *p >= 'A' && *p <= 'Z' ? (char)(*p + 32) : *p;
            char cn = *n >= 'A' && *n <= 'Z' ? (char)(*n + 32) : *n;
            if (ch != cn) break;
            n++; p++;
        }
        if (!*n) return true;
    }
    return false;
}

bool wm_start_menu_input_char(wm_state_t *state, char ch)
{
    if (!state || !state->launcher_open) return false;
    if (ch < 0x20 || state->search_len >= WM_SEARCH_MAX - 1u) return false;
    state->search_text[state->search_len++] = ch;
    state->search_text[state->search_len] = '\0';
    state->launcher_scroll = 0u;
    return true;
}

bool wm_start_menu_input_backspace(wm_state_t *state)
{
    if (!state || !state->launcher_open || state->search_len == 0u) return false;
    state->search_text[--state->search_len] = '\0';
    state->launcher_scroll = 0u;
    return true;
}

wm_rect_t wm_start_menu_rect(const wm_state_t *state)
{
    if (!state) return (wm_rect_t){0, 0, 0, 0};
    wm_rect_t dock = wm_taskbar_rect(state);
    uint32_t width  = wm_min_u32(480u, state->compositor.framebuffer_width);
#if WM_TASKBAR_AT_TOP
    uint32_t max_h = state->compositor.framebuffer_height >
        (uint32_t)(dock.y + (int32_t)dock.h + 16u) ?
        state->compositor.framebuffer_height -
        (uint32_t)(dock.y + (int32_t)dock.h) - 16u : 0u;
    uint32_t height = wm_min_u32(560u, max_h);
    int32_t y = dock.y + (int32_t)dock.h + 6;
#else
    uint32_t max_h = dock.y > 16 ? (uint32_t)dock.y - 16u : 0u;
    uint32_t height = wm_min_u32(560u, max_h);
    int32_t y = dock.y - (int32_t)height - 6;
    if (y < 0) y = 0;
#endif
    return (wm_rect_t){4, y, width, height};
}

#define SM_CARD_H   52u
#define SM_CARD_GAP  4u

static wm_rect_t app_viewport_rect(const wm_state_t *state)
{
    wm_rect_t menu = wm_start_menu_rect(state);
    int32_t y = menu.y + 136;
    uint32_t height = menu.h > 136u + 52u + 8u ?
        menu.h - 136u - 52u - 8u : 0u;
    uint32_t width  = menu.w > 24u ? menu.w - 24u : 0u;
    return (wm_rect_t){menu.x + 12, y, width, height};
}

static uint32_t visible_app_rows(const wm_state_t *state)
{
    wm_rect_t vp = app_viewport_rect(state);
    uint32_t stride = SM_CARD_H + SM_CARD_GAP;
    uint32_t rows = stride ? (vp.h + SM_CARD_GAP) / stride : 1u;
    return rows == 0u ? 1u : rows;
}

static uint32_t filtered_count(const wm_state_t *state)
{
    uint32_t count = 0u;
    for (uint32_t i = 0; i < state->assets.app_count; i++) {
        if (str_icontains(state->assets.apps[i].name, state->search_text))
            count++;
    }
    return count;
}

static uint32_t max_scroll_row(const wm_state_t *state)
{
    uint32_t total   = filtered_count(state);
    uint32_t visible = visible_app_rows(state);
    return total > visible ? total - visible : 0u;
}

void wm_start_menu_clamp_scroll(wm_state_t *state)
{
    if (!state) return;
    uint32_t max_scroll = max_scroll_row(state);
    if (state->launcher_scroll > max_scroll) state->launcher_scroll = max_scroll;
}

bool wm_start_menu_scroll(wm_state_t *state, int32_t rows)
{
    if (!state || !state->launcher_open || rows == 0) return false;
    uint32_t old = state->launcher_scroll;
    uint32_t max_scroll = max_scroll_row(state);
    int32_t next = (int32_t)state->launcher_scroll + rows;
    if (next < 0) next = 0;
    if ((uint32_t)next > max_scroll) next = (int32_t)max_scroll;
    state->launcher_scroll = (uint32_t)next;
    return old != state->launcher_scroll;
}

static wm_rect_t filtered_card_rect(const wm_state_t *state, uint32_t visual_row)
{
    wm_rect_t vp = app_viewport_rect(state);
    return (wm_rect_t){
        vp.x,
        vp.y + (int32_t)(visual_row * (SM_CARD_H + SM_CARD_GAP)),
        vp.w,
        SM_CARD_H
    };
}

static uint32_t blend_alpha(uint32_t base, uint32_t overlay, uint8_t a)
{
    uint32_t ia = 255u - a;
    uint32_t r = (((base>>16)&0xFF)*ia + ((overlay>>16)&0xFF)*(uint32_t)a)/255u;
    uint32_t g = (((base>>8)&0xFF)*ia  + ((overlay>>8)&0xFF)*(uint32_t)a)/255u;
    uint32_t b = ((base&0xFF)*ia       + (overlay&0xFF)*(uint32_t)a)/255u;
    return 0xFF000000u|(r<<16)|(g<<8)|b;
    (void)blend_alpha;
}

void wm_start_menu_draw(wm_state_t *state, wm_canvas_t *canvas)
{
    if (!state || !canvas || !state->launcher_open) return;
    wm_rect_t menu = wm_start_menu_rect(state);
    if (menu.w == 0u || menu.h == 0u) return;
    if (!wm_rect_intersects(menu, canvas->clip)) return;
    wm_start_menu_clamp_scroll(state);

    wm_canvas_fill_rounded(canvas,
        (wm_rect_t){menu.x + 3, menu.y + 5, menu.w, menu.h},
        16u, 0x1A000000u);
    wm_canvas_blur(canvas, menu, 10u);
    wm_canvas_fill_rounded(canvas, menu, 16u, state->theme.border);
    uint32_t menu_tint = (state->theme.surface & 0x00FFFFFFu) | (0xCCu << 24u);
    wm_canvas_fill_rounded(canvas,
        (wm_rect_t){menu.x + 1, menu.y + 1, menu.w - 2u, menu.h - 2u},
        15u, menu_tint);

    {
        wm_rect_t circle = {menu.x + 16, menu.y + 12, 32u, 32u};
        wm_canvas_fill_rounded(canvas, circle, 16u,
            state->theme.text | 0xFF000000u);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                wm_canvas_fill(canvas,
                    (wm_rect_t){circle.x + 9 + c*5, circle.y + 9 + r*5, 3u, 3u},
                    0xFFFFFFFFu);
        wm_font_draw(&state->font, canvas, menu.x + 58, menu.y + 13,
                     "Applications", state->theme.text, 15.0f,
                     menu.w > 80u ? menu.w - 80u : 0u);
        wm_font_draw(&state->font, canvas, menu.x + 58, menu.y + 34,
                     "ImplusOS Workspace", state->theme.text_dim, 10.0f,
                     menu.w > 80u ? menu.w - 80u : 0u);
    }

    wm_canvas_fill(canvas,
        (wm_rect_t){menu.x + 12, menu.y + 54, menu.w - 24u, 1u},
        state->theme.border);

    {
        wm_rect_t sb = {menu.x + 12, menu.y + 62, menu.w - 24u, 38u};
        wm_canvas_fill_rounded(canvas, sb, 12u, state->theme.surface_alt);
        uint32_t isz = 16u;
        wm_canvas_draw_icon(canvas,
            (wm_rect_t){sb.x + 10, sb.y + (int32_t)(sb.h - isz)/2, isz, isz},
            &state->assets.system_icons.search, 160u, 2u, state->theme.text_dim);
        const char *display = state->search_len > 0u ?
            state->search_text : "Search apps…";
        uint32_t tc = state->search_len > 0u ?
            state->theme.text : state->theme.text_dim;
        wm_font_draw(&state->font, canvas, sb.x + 34, sb.y + 10,
                     display, tc, 12.0f, sb.w > 44u ? sb.w - 44u : 0u);
        if (state->search_active && state->search_len > 0u) {
            uint32_t cursor_x = (uint32_t)(sb.x + 34) +
                wm_font_measure(&state->font, state->search_text, 12.0f);
            wm_canvas_fill(canvas,
                (wm_rect_t){(int32_t)cursor_x, sb.y + 9, 1u, 16u},
                state->theme.text | 0xFF000000u);
        }
        if (state->search_active) {
            wm_canvas_fill_rounded(canvas,
                (wm_rect_t){sb.x, sb.y + (int32_t)sb.h - 2, sb.w, 2u},
                1u, state->theme.accent | 0xFF000000u);
        }
    }

    {
        const char *label = state->search_len > 0u ? "Search results" : "All Apps";
        wm_font_draw(&state->font, canvas, menu.x + 14, menu.y + 110,
                     label, state->theme.text_dim, 10.0f,
                     menu.w > 28u ? menu.w - 28u : 0u);
    }

    {
        wm_rect_t vp = app_viewport_rect(state);
        wm_rect_t old_clip = canvas->clip;
        wm_canvas_set_clip(canvas, wm_rect_intersection(old_clip, vp));

        uint32_t visual_row = 0u;
        for (uint32_t i = 0; i < state->assets.app_count; i++) {
            wm_launcher_app_t *app = &state->assets.apps[i];
            if (!str_icontains(app->name, state->search_text)) continue;
            if (visual_row < state->launcher_scroll) { visual_row++; continue; }
            wm_rect_t card = filtered_card_rect(state,
                                visual_row - state->launcher_scroll);
            if (!wm_rect_intersects(card, vp)) { visual_row++; continue; }

            bool hover = state->launcher_hover_index == (int32_t)i;
            if (hover) {
                wm_canvas_fill_rounded(canvas, card, 10u, state->theme.surface_hover);
            }
            wm_rect_t icon_rect = {card.x + 8, card.y + 8, 36u, 36u};
            if (app->icon_pixels) {
                wm_canvas_blit_scaled(canvas, icon_rect, app->icon_pixels,
                                      app->icon_width, app->icon_height, 255u, 8u);
            } else {
                wm_canvas_fill_rounded(canvas, icon_rect, 10u,
                    state->theme.surface_alt);
                wm_font_draw(&state->font, canvas,
                             icon_rect.x + (int32_t)(icon_rect.w - 16u)/2,
                             icon_rect.y + (int32_t)(icon_rect.h - 13u)/2,
                             app->badge[0] ? app->badge : "?",
                             state->theme.text, 12.0f, 24u);
            }
            wm_font_draw(&state->font, canvas,
                         card.x + 54, card.y + 9,
                         app->name, state->theme.text, 12.0f,
                         card.w > 64u ? card.w - 64u : 0u);
            wm_font_draw(&state->font, canvas,
                         card.x + 54, card.y + 28,
                         "Application", state->theme.text_dim, 10.0f,
                         card.w > 64u ? card.w - 64u : 0u);
            visual_row++;
        }

        if (filtered_count(state) == 0u) {
            wm_font_draw(&state->font, canvas,
                         vp.x + 12, vp.y + 20,
                         "No results found.", state->theme.text_dim,
                         12.0f, vp.w > 24u ? vp.w - 24u : 0u);
        }

        uint32_t max_scroll = max_scroll_row(state);
        if (max_scroll > 0u && vp.h >= 24u) {
            wm_rect_t track = {vp.x + (int32_t)vp.w - 4, vp.y, 3u, vp.h};
            wm_canvas_fill_rounded(canvas, track, 2u, state->theme.border);
            uint32_t total_rows = filtered_count(state);
            uint32_t thumb_h = total_rows ?
                (uint32_t)(((uint64_t)visible_app_rows(state) * vp.h) / total_rows) : vp.h;
            if (thumb_h < 20u) thumb_h = 20u;
            if (thumb_h > vp.h) thumb_h = vp.h;
            uint32_t travel = vp.h - thumb_h;
            uint32_t thumb_y = max_scroll ?
                (uint32_t)(((uint64_t)state->launcher_scroll * travel) / max_scroll) : 0u;
            wm_canvas_fill_rounded(canvas,
                (wm_rect_t){track.x, track.y + (int32_t)thumb_y, 3u, thumb_h},
                2u, state->theme.text | 0xFF000000u);
        }
        canvas->clip = old_clip;
    }

    {
        wm_rect_t footer = {menu.x + 1, menu.y + (int32_t)menu.h - 52,
                            menu.w - 2u, 52u};
        wm_canvas_fill(canvas,
            (wm_rect_t){menu.x + 12, footer.y, menu.w - 24u, 1u},
            state->theme.border);
        wm_canvas_fill_rounded(canvas,
            (wm_rect_t){footer.x + 12, footer.y + 10, 32u, 32u},
            16u, state->theme.surface_alt);
        wm_font_draw(&state->font, canvas,
                     footer.x + 16, footer.y + 17,
                     "U", state->theme.text, 14.0f, 20u);
        wm_font_draw(&state->font, canvas,
                     footer.x + 52, footer.y + 16,
                     "ImplusOS User", state->theme.text, 12.0f,
                     menu.w > 180u ? menu.w - 180u : 0u);
        int32_t btn_right = menu.x + (int32_t)menu.w - 12;
        wm_rect_t sd = {btn_right - 32, footer.y + 10, 32u, 32u};
        wm_rect_t rb = {btn_right - 70, footer.y + 10, 32u, 32u};
        wm_canvas_fill_rounded(canvas, rb, 16u, state->theme.surface_hover);
        wm_canvas_fill_rounded(canvas, sd, 16u, state->theme.surface_hover);
        wm_canvas_draw_icon(canvas,
            (wm_rect_t){rb.x+7, rb.y+7, 18u, 18u},
            &state->assets.system_icons.reboot, 220u, 2u, state->theme.text);
        wm_canvas_draw_icon(canvas,
            (wm_rect_t){sd.x+7, sd.y+7, 18u, 18u},
            &state->assets.system_icons.power, 220u, 2u, state->theme.danger);
    }
}

wm_launcher_action_t wm_start_menu_hit_test(wm_state_t *state, int32_t x, int32_t y)
{
    wm_launcher_action_t result = {WM_LAUNCHER_ACTION_NONE, 0u};
    if (!state || !state->launcher_open) return result;
    wm_rect_t menu = wm_start_menu_rect(state);
    if (!wm_rect_intersects((wm_rect_t){x, y, 1u, 1u}, menu)) return result;

    int32_t btn_right = menu.x + (int32_t)menu.w - 12;
    int32_t footer_top = menu.y + (int32_t)menu.h - 52;
    if (y >= footer_top + 10 && y < footer_top + 42) {
        if (x >= btn_right - 70 && x < btn_right - 38) {
            result.kind = WM_LAUNCHER_ACTION_REBOOT;
            return result;
        }
        if (x >= btn_right - 32 && x < btn_right) {
            result.kind = WM_LAUNCHER_ACTION_SHUTDOWN;
            return result;
        }
    }

    wm_rect_t vp = app_viewport_rect(state);
    if (wm_rect_intersects((wm_rect_t){x, y, 1u, 1u}, vp)) {
        uint32_t visual_row = 0u;
        for (uint32_t i = 0; i < state->assets.app_count; i++) {
            if (!str_icontains(state->assets.apps[i].name, state->search_text))
                continue;
            if (visual_row < state->launcher_scroll) { visual_row++; continue; }
            wm_rect_t card = filtered_card_rect(state,
                                visual_row - state->launcher_scroll);
            if (!wm_rect_intersects(card, vp)) { visual_row++; continue; }
            if (wm_rect_intersects((wm_rect_t){x, y, 1u, 1u}, card)) {
                result.kind = WM_LAUNCHER_ACTION_APP;
                result.app_index = i;
                return result;
            }
            visual_row++;
        }
    }
    return result;
}