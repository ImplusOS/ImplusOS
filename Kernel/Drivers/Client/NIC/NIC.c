#include "NIC.h"

#include "Drivers/Module/DriverManager.h"

static volatile uint32_t g_poll_pending = 0;

static const driver_nic_t *get_nic_driver(void)
{
    return driver_manager_get_nic_driver();
}

bool nic_init(void)
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->init == NULL) {
        return false;
    }
    return driver->init();
}

bool nic_is_ready(void)
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->is_ready == NULL) {
        return false;
    }
    return driver->is_ready();
}

uint16_t nic_mtu(void)
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->mtu == NULL) {
        return 0;
    }
    return driver->mtu();
}

void nic_get_mac(uint8_t mac_out[6])
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->get_mac == NULL) {
        return;
    }
    driver->get_mac(mac_out);
}

bool nic_send_frame(const uint8_t *frame, uint16_t frame_len)
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->send_frame == NULL) {
        return false;
    }
    return driver->send_frame(frame, frame_len);
}

void nic_poll(void)
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->poll == NULL) {
        return;
    }
    driver->poll();
}

void nic_set_rx_callback(nic_rx_callback_t cb)
{
    const driver_nic_t *driver = get_nic_driver();
    if (driver == NULL || driver->set_rx_callback == NULL) {
        return;
    }
    driver->set_rx_callback(cb);
}

void nic_schedule_poll(void)
{
    __atomic_store_n(&g_poll_pending, 1u, __ATOMIC_RELEASE);
}

bool nic_check_poll(void)
{
    if (__atomic_exchange_n(&g_poll_pending, 0u, __ATOMIC_ACQ_REL) != 0u) {
        return true;
    }
    return false;
}
