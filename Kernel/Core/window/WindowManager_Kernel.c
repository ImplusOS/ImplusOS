#include "WindowManager_Kernel.h"
#include "IPC/IPC_Main.h"
#include "Drivers/Module/DriverManager.h"
#include "Network/network_main.h"
#include "Debug/serial/Serial.h"
#include <string.h>

typedef struct {
    uint32_t type;
    uint32_t request_id;
    uint32_t window_id;
} wm_msg_header_t;

static struct {
    int32_t  wm_service_pid;
    uint32_t display_width;
    uint32_t display_height;
    bool     initialized;
    bool     service_registered;
    volatile uint32_t in_timer;
} g_wm = {
    .wm_service_pid    = -1,
    .display_width     = 0,
    .display_height    = 0,
    .initialized       = false,
    .service_registered = false,
    .in_timer          = 0,
};

static void forward_keyboard_event(driver_keyboard_event_t *evt)
{
    if (!g_wm.service_registered) return;

    struct {
        wm_msg_header_t hdr;
        driver_keyboard_event_t event;
    } msg;

    memset(&msg, 0, sizeof(msg));
    msg.hdr.type      = WM_KEYBOARD_EVENT;
    msg.hdr.window_id = 0;
    msg.event         = *evt;

    ipc_send_message(g_wm.wm_service_pid, &msg, sizeof(msg));
}

static void forward_mouse_event(driver_mouse_event_t *evt)
{
    if (!g_wm.service_registered) {
        return;
    }

    struct {
        wm_msg_header_t hdr;
        driver_mouse_event_t event;
    } msg;

    memset(&msg, 0, sizeof(msg));
    msg.hdr.type      = WM_MOUSE_EVENT;
    msg.hdr.window_id = 0;
    msg.event         = *evt;

    ipc_send_message(g_wm.wm_service_pid, &msg, sizeof(msg));
}

void wm_kernel_init(void)
{
    if (g_wm.initialized) return;

    g_wm.display_width  = driver_manager_display_width();
    g_wm.display_height = driver_manager_display_height();

    g_wm.initialized = true;
}

void wm_kernel_register_service(int32_t pid)
{
    g_wm.wm_service_pid    = pid;
    g_wm.service_registered = true;
}

void wm_kernel_on_timer(void)
{
    if (!g_wm.initialized || !g_wm.service_registered) {
        return;
    }

    if (__atomic_exchange_n(&g_wm.in_timer, 1u, __ATOMIC_ACQUIRE)) {
        return;
    }

    driver_manager_input_ps2_poll();

    driver_keyboard_event_t kbd;
    while (driver_manager_input_ps2_read_keyboard(&kbd) > 0) {
        forward_keyboard_event(&kbd);
    }

    driver_mouse_event_t mouse;
    while (driver_manager_input_ps2_read_mouse(&mouse) > 0) {
        forward_mouse_event(&mouse);
    }
    
    driver_manager_input_usb_poll();
    driver_manager_input_usb_drain_keyboard(&kbd, &forward_keyboard_event);
    driver_manager_input_usb_drain_mouse(&mouse, &forward_mouse_event);
    network_stack_poll();

    __atomic_store_n(&g_wm.in_timer, 0u, __ATOMIC_RELEASE);
}

int32_t wm_kernel_get_wm_service_pid(void)
{
    return g_wm.wm_service_pid;
}

void wm_kernel_get_display_info(uint32_t *width, uint32_t *height)
{
    if (width)  *width  = g_wm.display_width;
    if (height) *height = g_wm.display_height;
}

bool wm_kernel_is_running(void)
{
    return g_wm.service_registered;
}
