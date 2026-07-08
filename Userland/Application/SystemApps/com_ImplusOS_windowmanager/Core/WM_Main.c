#include "../WindowManager.h"

#include "WM_Assets.h"
#include "WM_Display.h"
#include "WM_EventQueue.h"
#include "../Animation/WM_Animation.h"
#include "../Compositor/WM_Compositor.h"
#include "../Compositor/WM_Damage.h"
#include "../Font/WM_Font.h"
#include "../Input/WM_Input.h"
#include "../IPC/WM_IPC.h"
#include "../SceneGraph/WM_Node.h"
#include "../Theme/WM_Theme.h"
#include "../UI/WM_Notification.h"
#include "../UI/WM_StartMenu.h"
#include "../UI/WM_Taskbar.h"
#include "../../../../../Userland/Syscalls.h"
#include "../../../../../Userland/API/Process.h"
#include "../../../../../Userland/API/Serial.h"
#include "../../../../../Userland/API/Time.h"

#include <string.h>

wm_state_t g_wm_state;

#define CLOCK_UPDATE_INTERVAL_MS 1000u
#define WM_IPC_DRAIN_BUDGET 64u
#define WM_EVENT_PROCESS_BUDGET 64u
#define WM_POINTER_IDLE_POLL_MS 16u
#define WM_POINTER_ACTIVE_POLL_MS 4u
#define WM_POINTER_ACTIVE_WINDOW_MS 96u
#define WM_IDLE_SLEEP_MS 20u
#define WM_ACTIVE_SLEEP_MS 4u
#define WM_DEFERRED_START_DELAY_MS 500u
#define WM_DEFERRED_STEP_DELAY_MS 50u

static const char *const WM_FONT_PATH =
    "/Userland/SystemApps/com_ImplusOS_windowmanager"
    "/Resource/Fonts/NotoSansJP-Regular.ttf";

static void wm_serial_write_i32(int32_t value)
{
    if (value < 0) {
        serial_write_string("-");
        serial_write_uint32((uint32_t)(-value));
        return;
    }
    serial_write_uint32((uint32_t)value);
}

static wm_rect_t wm_service_bounds(const wm_state_t *state)
{
    if (!state) return (wm_rect_t){0, 0, 0, 0};
    return (wm_rect_t){0, 0,
        state->compositor.framebuffer_width,
        state->compositor.framebuffer_height};
}

static wm_rect_t wm_service_expand_rect(wm_rect_t rect, int32_t amount)
{
    return (wm_rect_t){
        rect.x - amount,
        rect.y - amount,
        rect.w + (uint32_t)(amount * 2),
        rect.h + (uint32_t)(amount * 2)
    };
}

static void wm_service_damage_rect(wm_state_t *state, wm_rect_t rect)
{
    if (!state || rect.w == 0u || rect.h == 0u) return;
    wm_region_add(&state->compositor.damage, rect,
                  wm_service_bounds(state));
}

static void wm_service_damage_visible_chrome(wm_state_t *state)
{
    if (!state) return;
    wm_service_damage_rect(state,
        wm_service_expand_rect(wm_taskbar_rect(state), 6));
    if (state->launcher_open) {
        wm_service_damage_rect(state,
            wm_service_expand_rect(wm_start_menu_rect(state), 8));
    }
    if (state->notification_center_open) {
        wm_service_damage_rect(state,
            wm_service_expand_rect(wm_notification_center_rect(state), 8));
    }
}

static bool wm_service_do_deferred_work(wm_state_t *state)
{
    if (!state) return false;

    if (!state->font.loaded) {
        (void)wm_font_init(&state->font, WM_FONT_PATH);
        wm_compositor_damage_all(state);
        return true;
    }

    if (state->assets.metadata_loaded && !state->assets.icons_loaded) {
        bool loaded_one = wm_assets_load_next_icon(&state->assets);
        if (loaded_one) {
            wm_service_damage_visible_chrome(state);
            return true;
        }
    }

    if (state->assets.metadata_loaded &&
        !state->assets.wallpaper_load_attempted) {
        (void)wm_assets_reload_wallpaper(&state->assets);
        wm_compositor_generate_background(state);
        wm_compositor_damage_all(state);
        return true;
    }

    return false;
}

void wm_service_init(wm_state_t *state)
{
    if (!state) return;
    serial_write_string("[wm] init start\n");
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

    (void)wm_assets_init_metadata(&state->assets);

    (void)wm_display_update_from_system(state);
    uint32_t width  = state->display_topology.width;
    uint32_t height = state->display_topology.height;
    if (width == 0u || height == 0u) {
        width = get_display_width();
        height = get_display_height();
        wm_display_set_fallback(state, width, height);
    }
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
    int32_t register_status = window_register_service();
    serial_write_string("[wm] register status=");
    wm_serial_write_i32(register_status);
    serial_write_string("\n");
}

void wm_service_main_loop(void)
{
    wm_service_init(&g_wm_state);
    if (!g_wm_state.running) {
        for (;;) sleep_ms(1000u);
    }

    uint64_t last_clock_ms = 0u;
    uint64_t last_display_poll_ms = 0u;
    uint64_t last_pointer_poll_ms = 0u;
    uint64_t pointer_active_until_ms = 0u;
    uint64_t next_deferred_work_ms = get_uptime_ms() + WM_DEFERRED_START_DELAY_MS;

    while (g_wm_state.running) {
        bool did_work = false;
        ipc_message_t incoming;
        uint32_t ipc_drained = 0u;
        while (ipc_drained < WM_IPC_DRAIN_BUDGET &&
               ipc_receive_message(&incoming) == 0) {
            did_work = true;
            ++ipc_drained;
            if (!wm_event_queue_push_coalesced(&g_wm_state.event_queue,
                                               &incoming)) {
                ipc_message_t oldest;
                if (wm_event_queue_pop(&g_wm_state.event_queue, &oldest))
                    wm_ipc_handle_message(&g_wm_state, &oldest);
                if (!wm_event_queue_push_coalesced(&g_wm_state.event_queue,
                                                   &incoming))
                    wm_ipc_handle_message(&g_wm_state, &incoming);
            }
        }
        ipc_message_t event;
        uint32_t events_processed = 0u;
        while (events_processed < WM_EVENT_PROCESS_BUDGET &&
               wm_event_queue_pop(&g_wm_state.event_queue, &event)) {
            did_work = true;
            ++events_processed;
            wm_ipc_handle_message(&g_wm_state, &event);
        }

        uint64_t now_ms = get_uptime_ms();
        uint32_t pointer_poll_ms =
            now_ms < pointer_active_until_ms ?
                WM_POINTER_ACTIVE_POLL_MS : WM_POINTER_IDLE_POLL_MS;
        if (now_ms - last_pointer_poll_ms >= pointer_poll_ms) {
            last_pointer_poll_ms = now_ms;
            if (wm_input_poll_kernel_pointer(&g_wm_state)) {
                did_work = true;
                pointer_active_until_ms = now_ms + WM_POINTER_ACTIVE_WINDOW_MS;
            }
        }

        if (now_ms - last_display_poll_ms >= 250u) {
            last_display_poll_ms = now_ms;
            (void)wm_display_reconfigure_if_needed(&g_wm_state);
        }
        if (!did_work && now_ms >= next_deferred_work_ms &&
            now_ms >= pointer_active_until_ms &&
            wm_service_do_deferred_work(&g_wm_state)) {
            did_work = true;
            next_deferred_work_ms = now_ms + WM_DEFERRED_STEP_DELAY_MS;
        }
        if (now_ms - last_clock_ms >= CLOCK_UPDATE_INTERVAL_MS) {
            last_clock_ms = now_ms;
            if (wm_taskbar_update_clock(&g_wm_state)) {
                wm_rect_t clock_rect = wm_taskbar_clock_rect(&g_wm_state);
                wm_region_add(&g_wm_state.compositor.damage, clock_rect,
                    (wm_rect_t){0, 0, g_wm_state.compositor.framebuffer_width,
                                 g_wm_state.compositor.framebuffer_height});
                g_wm_state.compositor.next_frame_ms = 0u;
                did_work = true;
            }
        }
        
        bool animating     = wm_animation_tick(&g_wm_state, now_ms);
        bool notifications = wm_notification_tick(&g_wm_state, now_ms);

        bool pending_frame = wm_compositor_has_pending_frame(&g_wm_state);
        if (now_ms >= g_wm_state.compositor.next_frame_ms &&
            (animating || notifications || pending_frame)) {
            wm_compositor_render(&g_wm_state, now_ms);
            g_wm_state.compositor.next_frame_ms = now_ms + 16u;
            did_work = true;
        } else if (!animating && !notifications && !pending_frame) {
            sleep_ms((did_work || now_ms < pointer_active_until_ms) ?
                         WM_ACTIVE_SLEEP_MS : WM_IDLE_SLEEP_MS);
            continue;
        }
        sleep_ms((did_work || now_ms < pointer_active_until_ms) ?
                     1u : WM_ACTIVE_SLEEP_MS);
    }
}
