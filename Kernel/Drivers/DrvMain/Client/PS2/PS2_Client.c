#include "PS2_Input.h"

#include "../../../Module/DriverBinary.h"
#include "../../../Module/DriverManager.h"

#include <stdbool.h>
#include <stdint.h>

static const driver_input_t *g_ps2_driver = NULL;
static uint8_t g_ps2_initialized = 0;

static bool ensure_ps2_initialized(void)
{
    const driver_input_t *driver = driver_manager_get_ps2_driver();

    if (driver == NULL) {
        g_ps2_driver = NULL;
        g_ps2_initialized = 0;
        return false;
    }

    if (g_ps2_driver != driver) {
        g_ps2_driver = driver;
        g_ps2_initialized = 0;
    }

    if (g_ps2_initialized) {
        return true;
    }

    if (g_ps2_driver->init) {
        g_ps2_driver->init();
    }
    g_ps2_initialized = 1;
    return true;
}

bool ps2_input_init(void)
{
    return ensure_ps2_initialized();
}

void ps2_input_poll(void)
{
    if (!ensure_ps2_initialized()) {
        return;
    }
    g_ps2_driver->poll();
}

int32_t ps2_input_read_keyboard(driver_keyboard_event_t *out_event)
{
    if (out_event == NULL) {
        return -1;
    }
    if (!ensure_ps2_initialized()) {
        return 0;
    }
    return g_ps2_driver->read_keyboard(out_event);
}

int32_t ps2_input_read_mouse(driver_mouse_event_t *out_event)
{
    if (out_event == NULL) {
        return -1;
    }
    if (!ensure_ps2_initialized()) {
        return 0;
    }
    return g_ps2_driver->read_mouse(out_event);
}
