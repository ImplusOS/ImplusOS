#include "USB_Driver_API.h"
#include "Drivers/Module/DriverBinary.h"
#include "Drivers/Module/DriverManager.h"
#include "Debug/serial/Serial.h"

static const usb_master_vtable_t *g_usb_vtable = NULL;
static volatile uint32_t g_usb_poll_pending = 0;

static bool usb_driver_client_refresh(void)
{
    g_usb_vtable = driver_manager_get_usb_driver();
    return (g_usb_vtable != NULL);
}

void usb_driver_client_init(void)
{
    if (!usb_driver_client_refresh()) {
        return;
    }

    if (g_usb_vtable->input.init) {
        g_usb_vtable->input.init();
    }
}

bool usb_driver_client_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    if (!usb_driver_client_refresh()) {
        return false;
    }
    if (g_usb_vtable == NULL || g_usb_vtable->storage.read_sectors == NULL) {
        return false;
    }
    return g_usb_vtable->storage.read_sectors(lba, buffer, sectors);
}

bool usb_driver_client_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors)
{
    if (!usb_driver_client_refresh()) {
        return false;
    }
    if (g_usb_vtable == NULL || g_usb_vtable->storage.write_sectors == NULL) {
        return false;
    }
    return g_usb_vtable->storage.write_sectors(lba, buffer, sectors);
}

int32_t usb_driver_client_read_keyboard(driver_keyboard_event_t *out_event)
{
    if (!usb_driver_client_refresh()) {
        return 0;
    }
    if (g_usb_vtable == NULL) {
        return 0;
    }
    if (g_usb_vtable->input.read_keyboard == NULL) {
        return 0;
    }
    return g_usb_vtable->input.read_keyboard(out_event);
}

int32_t usb_driver_client_read_mouse(driver_mouse_event_t *out_event)
{
    if (!usb_driver_client_refresh()) {
        return 0;
    }
    if (g_usb_vtable == NULL || g_usb_vtable->input.read_mouse == NULL) {
        return 0;
    }
    return g_usb_vtable->input.read_mouse(out_event);
}

void usb_driver_client_poll(void)
{
    if (!usb_driver_client_refresh()) {
        return;
    }
    if (g_usb_vtable == NULL || g_usb_vtable->input.poll == NULL) {
        return;
    }
    g_usb_vtable->input.poll();
}

void usb_driver_client_drain_keyboard(driver_keyboard_event_t *tmp,
                                      void (*forward)(driver_keyboard_event_t *))
{
    if (!usb_driver_client_refresh()) {
        return;
    }
    if (g_usb_vtable == NULL || g_usb_vtable->input.drain_keyboard == NULL) {
        return;
    }
    g_usb_vtable->input.drain_keyboard(tmp, forward);
}

void usb_driver_client_drain_mouse(driver_mouse_event_t *tmp,
                                   void (*forward)(driver_mouse_event_t *))
{
    if (!usb_driver_client_refresh()) {
        return;
    }
    if (g_usb_vtable == NULL || g_usb_vtable->input.drain_mouse == NULL) {
        return;
    }
    g_usb_vtable->input.drain_mouse(tmp, forward);
}

void usb_driver_client_schedule_poll(void)
{
    __atomic_store_n(&g_usb_poll_pending, 1u, __ATOMIC_RELEASE);
}

bool usb_driver_client_check_poll(void)
{
    if (__atomic_exchange_n(&g_usb_poll_pending, 0u, __ATOMIC_ACQ_REL)) {
        return true;
    }
    return false;
}
