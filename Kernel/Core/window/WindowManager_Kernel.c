#include "WindowManager_Kernel.h"
#include "IPC/IPC_Main.h"
#include "Drivers/Module/DriverManager.h"
#include "Drivers/Module/InputManager.h"
#include "Core/process/ProcessManager.h"
#include "Debug/serial/Serial.h"
#include "Core/sync/Spinlock.h"
#include <string.h>

typedef struct {
    uint32_t type;
    uint32_t request_id;
    uint32_t window_id;
} wm_msg_header_t;

#define WM_KERNEL_MOUSE_TRACE 0u
#define WM_KERNEL_MOUSE_DEBUG_LIMIT 8u
#define WM_KERNEL_MOUSE_SUMMARY_INTERVAL 128ULL
#define WM_KERNEL_INPUT_EXTRA_POLL_BUDGET 1u
#define WM_KERNEL_TIMER_WAKE_DIVIDER 2u

typedef struct {
    int32_t  dx;
    int32_t  dy;
    uint8_t  buttons;
    int8_t   wheel;
    uint32_t count;
} wm_mouse_accumulator_t;

static struct {
    int32_t  wm_service_pid;
    uint32_t display_width;
    uint32_t display_height;
    bool     initialized;
    bool     service_registered;
    volatile uint32_t in_timer;
    spinlock_t lock;
    uint32_t pointer_x;
    uint32_t pointer_y;
    uint8_t  pointer_buttons;
    int8_t   pointer_wheel;
    uint64_t pointer_sequence;
} g_wm = {
    .wm_service_pid    = -1,
    .display_width     = 0,
    .display_height    = 0,
    .initialized       = false,
    .service_registered = false,
    .in_timer          = 0,
    .pointer_x         = 0,
    .pointer_y         = 0,
    .pointer_buttons   = 0,
    .pointer_wheel     = 0,
    .pointer_sequence  = 0,
};

static uint32_t g_mouse_forward_debug_count = 0u;
static uint64_t g_mouse_forward_total = 0u;
static uint64_t g_mouse_forward_next_summary = WM_KERNEL_MOUSE_SUMMARY_INTERVAL;
static wm_mouse_accumulator_t g_mouse_accum = {0};

static void wm_debug_i32(int32_t value)
{
    if (value < 0) {
        serial_write_string("-");
        serial_write_uint32((uint32_t)(-value));
    } else {
        serial_write_uint32((uint32_t)value);
    }
}

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

static void apply_mouse_delta(int32_t dx, int32_t dy, uint8_t buttons,
                              int8_t wheel, uint32_t coalesced_count)
{
    if (!g_wm.service_registered) {
        return;
    }

    uint32_t current_width = 0u;
    uint32_t current_height = 0u;
    if (g_wm.display_width == 0u || g_wm.display_height == 0u) {
        current_width = driver_manager_display_width();
        current_height = driver_manager_display_height();
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_wm.lock);

    if (current_width != 0u) {
        g_wm.display_width = current_width;
    }
    if (current_height != 0u) {
        g_wm.display_height = current_height;
    }

    int32_t x = (int32_t)g_wm.pointer_x + dx;
    int32_t y = (int32_t)g_wm.pointer_y + dy;
    uint32_t width = g_wm.display_width;
    uint32_t height = g_wm.display_height;
    if (width == 0u) width = 1u;
    if (height == 0u) height = 1u;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int32_t)width) x = (int32_t)width - 1;
    if (y >= (int32_t)height) y = (int32_t)height - 1;

    g_wm.pointer_x = (uint32_t)x;
    g_wm.pointer_y = (uint32_t)y;
    g_wm.pointer_buttons = buttons;
    g_wm.pointer_wheel = wheel;
    g_wm.pointer_sequence++;
    uint64_t sequence = g_wm.pointer_sequence;
    g_mouse_forward_total += (coalesced_count == 0u) ? 1u : coalesced_count;

    spinlock_unlock(&g_wm.lock);
    irq_restore(irq_flags);

    if (WM_KERNEL_MOUSE_TRACE != 0u &&
        (g_mouse_forward_debug_count < WM_KERNEL_MOUSE_DEBUG_LIMIT ||
         g_mouse_forward_total >= g_mouse_forward_next_summary)) {
        bool summary = g_mouse_forward_debug_count >= WM_KERNEL_MOUSE_DEBUG_LIMIT;
        if (!summary) {
            ++g_mouse_forward_debug_count;
        } else {
            while (g_mouse_forward_next_summary <= g_mouse_forward_total) {
                g_mouse_forward_next_summary += WM_KERNEL_MOUSE_SUMMARY_INTERVAL;
            }
        }
        serial_write_string(summary ?
                            "[wm:kernel] mouse summary dx=" :
                            "[wm:kernel] mouse dx=");
        wm_debug_i32(dx);
        serial_write_string(" dy=");
        wm_debug_i32(dy);
        serial_write_string(" x=");
        serial_write_uint32((uint32_t)x);
        serial_write_string(" y=");
        serial_write_uint32((uint32_t)y);
        serial_write_string(" buttons=");
        serial_write_uint32(buttons);
        serial_write_string(" seq=");
        serial_write_uint32((uint32_t)sequence);
        serial_write_string(" total=");
        serial_write_uint32((uint32_t)g_mouse_forward_total);
        serial_write_string(" events=");
        serial_write_uint32(coalesced_count);
        serial_write_string("\n");
    }

    /*
     * Mouse motion is exposed to the WM through the kernel pointer state.
     * Sending every motion as IPC as well can leave stale mouse messages in
     * the WM queue and make the visible cursor jump backwards under load.
     */
}

static void collect_mouse_event(driver_mouse_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    g_mouse_accum.dx += (int32_t)(int16_t)evt->x;
    g_mouse_accum.dy += (int32_t)(int16_t)evt->y;
    g_mouse_accum.buttons = evt->buttons;
    g_mouse_accum.wheel = evt->wheel;
    ++g_mouse_accum.count;
}

static uint32_t flush_collected_mouse_events(void)
{
    if (g_mouse_accum.count == 0u) {
        return 0u;
    }

    uint32_t count = g_mouse_accum.count;
    apply_mouse_delta(g_mouse_accum.dx,
                      g_mouse_accum.dy,
                      g_mouse_accum.buttons,
                      g_mouse_accum.wheel,
                      g_mouse_accum.count);
    memset(&g_mouse_accum, 0, sizeof(g_mouse_accum));
    return count;
}

static uint32_t drain_queued_input_once(driver_keyboard_event_t *kbd,
                                        driver_mouse_event_t *mouse)
{
    input_manager_drain_keyboard(kbd, &forward_keyboard_event);
    memset(&g_mouse_accum, 0, sizeof(g_mouse_accum));
    input_manager_drain_mouse(mouse, &collect_mouse_event);
    return flush_collected_mouse_events();
}

void wm_kernel_init(void)
{
    if (g_wm.initialized) return;

    g_wm.display_width  = driver_manager_display_width();
    g_wm.display_height = driver_manager_display_height();
    spinlock_init(&g_wm.lock);
    g_wm.pointer_x = g_wm.display_width / 2u;
    g_wm.pointer_y = g_wm.display_height / 2u;
    g_wm.pointer_buttons = 0u;
    g_wm.pointer_wheel = 0;
    g_wm.pointer_sequence = 1u;
    g_mouse_forward_debug_count = 0u;
    g_mouse_forward_total = 0u;
    g_mouse_forward_next_summary = WM_KERNEL_MOUSE_SUMMARY_INTERVAL;

    g_wm.initialized = true;
}

void wm_kernel_register_service(int32_t pid)
{
    uint32_t current_width = driver_manager_display_width();
    uint32_t current_height = driver_manager_display_height();

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_wm.lock);
    if (current_width != 0u) {
        g_wm.display_width = current_width;
    }
    if (current_height != 0u) {
        g_wm.display_height = current_height;
    }
    if (g_wm.pointer_sequence <= 1u &&
        g_wm.display_width != 0u &&
        g_wm.display_height != 0u) {
        g_wm.pointer_x = g_wm.display_width / 2u;
        g_wm.pointer_y = g_wm.display_height / 2u;
    }
    spinlock_unlock(&g_wm.lock);
    irq_restore(irq_flags);

    g_wm.wm_service_pid    = pid;
    g_wm.service_registered = true;

    serial_write_string("[wm:kernel] service registered pid=");
    wm_debug_i32(pid);
    serial_write_string("\n");
}

void wm_kernel_on_timer(void)
{
    /*
     * Keep timer IRQ work bounded. The timer does not drain USB/input here;
     * it only prevents the WM from depending on unrelated serial/debug output
     * to leave sleep and poll pointer state.
     */
    static uint32_t wake_tick = 0u;
    if (!g_wm.initialized || !g_wm.service_registered ||
        g_wm.wm_service_pid < 0) {
        return;
    }
    ++wake_tick;
    if (WM_KERNEL_TIMER_WAKE_DIVIDER > 1u &&
        (wake_tick % WM_KERNEL_TIMER_WAKE_DIVIDER) != 0u) {
        return;
    }
    (void)process_wake_pid(g_wm.wm_service_pid);
}

void wm_kernel_drain_input(void)
{
    if (!g_wm.initialized || !g_wm.service_registered) {
        return;
    }

    if (__atomic_exchange_n(&g_wm.in_timer, 1u, __ATOMIC_ACQUIRE)) {
        return;
    }

    driver_keyboard_event_t kbd;
    driver_mouse_event_t mouse;
    uint32_t mouse_events = drain_queued_input_once(&kbd, &mouse);
    for (uint32_t i = 0u;
         i < WM_KERNEL_INPUT_EXTRA_POLL_BUDGET && mouse_events != 0u;
         ++i) {
        input_manager_poll();
        mouse_events = drain_queued_input_once(&kbd, &mouse);
    }
    __atomic_store_n(&g_wm.in_timer, 0u, __ATOMIC_RELEASE);
}

int32_t wm_kernel_get_wm_service_pid(void)
{
    return g_wm.wm_service_pid;
}

void wm_kernel_get_display_info(uint32_t *width, uint32_t *height)
{
    g_wm.display_width  = driver_manager_display_width();
    g_wm.display_height = driver_manager_display_height();
    if (width)  *width  = g_wm.display_width;
    if (height) *height = g_wm.display_height;
}

void wm_kernel_get_pointer_state(wm_kernel_pointer_state_t *state_out)
{
    if (state_out == NULL) {
        return;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_wm.lock);
    state_out->x = g_wm.pointer_x;
    state_out->y = g_wm.pointer_y;
    state_out->buttons = g_wm.pointer_buttons;
    state_out->wheel = g_wm.pointer_wheel;
    state_out->reserved[0] = 0u;
    state_out->reserved[1] = 0u;
    state_out->sequence = g_wm.pointer_sequence;
    spinlock_unlock(&g_wm.lock);
    irq_restore(irq_flags);
}

bool wm_kernel_is_running(void)
{
    return g_wm.service_registered;
}
