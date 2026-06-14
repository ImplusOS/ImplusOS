#include "../WindowManager.h"

#include "WM_Assets.h"
#include "WM_EventQueue.h"
#include "../Animation/WM_Animation.h"
#include "../Compositor/WM_Compositor.h"
#include "../Compositor/WM_Damage.h"
#include "../Font/WM_Font.h"
#include "../IPC/WM_IPC.h"
#include "../SceneGraph/WM_Node.h"
#include "../Theme/WM_Theme.h"
#include "../UI/WM_Notification.h"
#include "../UI/WM_Taskbar.h"
#include "../../../../../Userland/Syscalls.h"
#include "../../../../../Userland/API/Process.h"
#include "../../../../../Userland/API/Time.h"

#include <string.h>

wm_state_t g_wm_state;

#define CLOCK_UPDATE_INTERVAL_MS 1000u

void wm_service_init(wm_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->launcher_hover_index = -1;
    state->running = true;
    wm_scene_init(&state->scene);
    wm_event_queue_init(&state->event_queue);
    wm_theme_set_defaults(&state->theme);

    if (!wm_theme_load(&state->theme,
            "/Userland/SystemApps/com_ImplusOS_windowmanager"
            "/Resource/Themes/plasma.theme")) {
        (void)wm_theme_load(&state->theme,
            "/Userland/SystemApps/com_ImplusOS_windowmanager"
            "/Resource/Themes/midnight.theme");
    }

    (void)wm_assets_init(&state->assets);
    (void)wm_font_init(&state->font,
        "/Userland/SystemApps/com_ImplusOS_windowmanager"
        "/Resource/Fonts/NotoSansJP-Regular.ttf");

    uint32_t width  = get_display_width();
    uint32_t height = get_display_height();
    if (!wm_compositor_init(state, width, height)) {
        state->running = false;
        return;
    }
    state->scene.cursor_x       = width  / 2u;
    state->scene.cursor_y       = height / 2u;
    state->scene.cursor_visible = true;
    state->scene.cursor_style   = WM_CURSOR_DEFAULT;

    (void)wm_taskbar_update_clock(state);

    wm_compositor_generate_background(state);
    wm_compositor_damage_all(state);
    state->compositor.next_frame_ms = get_uptime_ms();
    window_register_service();
}

void wm_service_main_loop(void)
{
    draw_fill_rect(0u, 0u, get_display_width(), get_display_height(), 0xFFEEEEEEu);
    draw_present();
    wm_service_init(&g_wm_state);
    if (!g_wm_state.running) {
        for (;;) process_yield();
    }

    uint64_t last_clock_ms = 0u;

    while (g_wm_state.running) {
        ipc_message_t incoming;
        while (ipc_receive_message(&incoming) == 0) {
            if (!wm_event_queue_push(&g_wm_state.event_queue, &incoming)) {
                ipc_message_t oldest;
                if (wm_event_queue_pop(&g_wm_state.event_queue, &oldest))
                    wm_ipc_handle_message(&g_wm_state, &oldest);
                if (!wm_event_queue_push(&g_wm_state.event_queue, &incoming))
                    wm_ipc_handle_message(&g_wm_state, &incoming);
            }
        }
        ipc_message_t event;
        while (wm_event_queue_pop(&g_wm_state.event_queue, &event))
            wm_ipc_handle_message(&g_wm_state, &event);

        uint64_t now_ms = get_uptime_ms();
        if (now_ms - last_clock_ms >= CLOCK_UPDATE_INTERVAL_MS) {
            last_clock_ms = now_ms;
            if (wm_taskbar_update_clock(&g_wm_state)) {
                wm_rect_t dock = wm_taskbar_rect(&g_wm_state);
                wm_region_add(&g_wm_state.compositor.damage, dock,
                    (wm_rect_t){0, 0, g_wm_state.compositor.framebuffer_width,
                                 g_wm_state.compositor.framebuffer_height});
            }
        }
        
        bool animating     = wm_animation_tick(&g_wm_state, now_ms);
        bool notifications = wm_notification_tick(&g_wm_state, now_ms);

        bool pending_frame = wm_compositor_has_pending_frame(&g_wm_state);
        if (now_ms >= g_wm_state.compositor.next_frame_ms &&
            (animating || notifications || pending_frame)) {
            wm_compositor_render(&g_wm_state, now_ms);
            g_wm_state.compositor.next_frame_ms = now_ms + 16u;
        } else if (!animating && !notifications && !pending_frame) {
            process_yield();
            continue;
        }
        process_yield();
    }
}