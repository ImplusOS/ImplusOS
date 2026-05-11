#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "kernel/status.h"

#define WM_CREATE_WINDOW        1
#define WM_DESTROY_WINDOW       2
#define WM_SET_WINDOW_RECT      3
#define WM_SHOW_WINDOW          4
#define WM_HIDE_WINDOW          5
#define WM_RAISE_WINDOW         6
#define WM_LOWER_WINDOW         7
#define WM_SET_FOCUS            8

#define WM_DRAW_PIXEL          10
#define WM_SET_WINDOW_SYSTEM   15
#define WM_DRAW_RECT           11
#define WM_CLEAR_WINDOW        12
#define WM_UPDATE_COMPLETE     13
#define WM_BLIT_BUFFER         14

#define WM_KEYBOARD_EVENT      20
#define WM_MOUSE_EVENT         21

#define WM_SUBSCRIBE_INPUT     30
#define WM_UNSUBSCRIBE_INPUT   31

#define WM_GET_WINDOW_RECT     40
#define WM_WINDOW_CREATED      41
#define WM_WINDOW_DESTROYED    42
#define WM_WINDOW_MOVED        43

#define WM_REGISTER_SERVICE    50
#define WM_GET_DISPLAY_INFO    51
#define WM_GET_FOCUS           52

void wm_kernel_init(void);
void wm_kernel_register_service(int32_t pid);
void wm_kernel_on_timer(void);
int32_t wm_kernel_get_wm_service_pid(void);
void wm_kernel_get_display_info(uint32_t *width, uint32_t *height);
bool wm_kernel_is_running(void);