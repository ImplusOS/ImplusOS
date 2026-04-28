#include "NIC.h"

#include "Drivers/Module/DriverManager.h"
#include "Drivers/DrvMain/Server/NIC/VirtIONet/VirtIONet.h"

static volatile uint32_t g_poll_pending = 0;

static bool nic_driver_init_wrapper(void)
{
    return virtio_net_init();
}

static bool nic_driver_is_ready_wrapper(void)
{
    return virtio_net_is_ready();
}

static uint16_t nic_driver_mtu_wrapper(void)
{
    return virtio_net_mtu();
}

static void nic_driver_get_mac_wrapper(uint8_t mac_out[6])
{
    virtio_net_get_mac(mac_out);
}

static bool nic_driver_send_frame_wrapper(const uint8_t *frame, uint16_t frame_len)
{
    return virtio_net_send(frame, frame_len);
}

static void nic_driver_poll_wrapper(void)
{
    virtio_net_poll();
}

static void nic_driver_set_rx_callback_wrapper(driver_nic_rx_callback_t cb)
{
    virtio_net_set_rx_callback(cb);
}

static const driver_nic_t g_virtio_nic_driver = {
    .init = nic_driver_init_wrapper,
    .is_ready = nic_driver_is_ready_wrapper,
    .mtu = nic_driver_mtu_wrapper,
    .get_mac = nic_driver_get_mac_wrapper,
    .send_frame = nic_driver_send_frame_wrapper,
    .poll = nic_driver_poll_wrapper,
    .set_rx_callback = nic_driver_set_rx_callback_wrapper,
};

static const driver_nic_t *get_nic_driver(void)
{
    const driver_nic_t *driver = driver_manager_get_nic_driver();
    if (driver != NULL) {
        return driver;
    }

    if (!driver_manager_attach("VirtIONet_Builtin",
                               DRIVER_MANAGER_KIND_NIC,
                               &g_virtio_nic_driver)) {
        return NULL;
    }

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
