#include "WM_Animation.h"

#include "../SceneGraph/WM_Node.h"

static float ease_out_cubic(float value)
{
    float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
}

static float ease_in_cubic(float value)
{
    return value * value * value;
}

void wm_animation_start(wm_state_t *state, wm_window_t *window,
                        wm_transition_t transition, uint32_t duration_ms)
{
    if (!state || !window) return;
    extern uint64_t get_uptime_ms(void);
    wm_window_mark_frame_damage(state, window);
    window->transition = transition;
    window->transition_started_ms = get_uptime_ms();
    window->transition_duration_ms = duration_ms == 0u ? 1u : duration_ms;
    if (transition == WM_TRANSITION_SHOW || transition == WM_TRANSITION_RESTORE) {
        window->visual_alpha = 0.0f;
        window->visual_scale = 0.92f;
        window->visual_offset_y = 14.0f;
    } else {
        window->visual_alpha = 1.0f;
        window->visual_scale = 1.0f;
        window->visual_offset_y = 0.0f;
    }
    wm_window_mark_frame_damage(state, window);
}

bool wm_animation_tick(wm_state_t *state, uint64_t now_ms)
{
    if (!state) return false;
    bool active = false;
    for (uint32_t id = 1u; id <= WM_MAX_WINDOWS; ++id) {
        wm_window_t *window = state->scene.id_table[id];
        if (!window || window->transition == WM_TRANSITION_NONE) continue;
        active = true;
        wm_window_mark_frame_damage(state, window);
        uint64_t elapsed = now_ms - window->transition_started_ms;
        float progress = elapsed >= window->transition_duration_ms ? 1.0f :
            (float)elapsed / (float)window->transition_duration_ms;
        bool appearing = window->transition == WM_TRANSITION_SHOW ||
                         window->transition == WM_TRANSITION_RESTORE;
        float eased = appearing ? ease_out_cubic(progress) : ease_in_cubic(progress);
        if (appearing) {
            window->visual_alpha = eased;
            window->visual_scale = 0.92f + 0.08f * eased;
            window->visual_offset_y = 14.0f * (1.0f - eased);
        } else {
            window->visual_alpha = 1.0f - eased;
            window->visual_scale = 1.0f - 0.05f * eased;
            window->visual_offset_y = 10.0f * eased;
        }
        wm_window_mark_frame_damage(state, window);
        if (progress < 1.0f) continue;

        wm_transition_t completed = window->transition;
        window->transition = WM_TRANSITION_NONE;
        if (appearing) {
            window->visual_alpha = 1.0f;
            window->visual_scale = 1.0f;
            window->visual_offset_y = 0.0f;
        } else if (completed == WM_TRANSITION_CLOSE) {
            wm_scene_destroy_immediate(state, id);
        } else {
            window->visual_alpha = 0.0f;
            window->visual_scale = 0.95f;
            if (completed == WM_TRANSITION_HIDE) window->visible = false;
            if (completed == WM_TRANSITION_MINIMIZE) window->minimized = true;
        }
    }
    return active;
}
