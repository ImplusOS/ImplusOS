#include "NicManager.h"

#include "Drivers/Client/NIC/NIC.h"

bool nic_manager_init(void)
{
    return nic_init();
}

bool nic_manager_is_ready(void)
{
    return nic_is_ready();
}

uint16_t nic_manager_mtu(void)
{
    return nic_mtu();
}

void nic_manager_get_mac(uint8_t mac_out[6])
{
    nic_get_mac(mac_out);
}

bool nic_manager_send_frame(const uint8_t *frame, uint16_t frame_len)
{
    return nic_send_frame(frame, frame_len);
}

void nic_manager_poll(void)
{
    nic_poll();
}

void nic_manager_set_rx_callback(driver_nic_rx_callback_t cb)
{
    nic_set_rx_callback(cb);
}

void nic_manager_schedule_poll(void)
{
    nic_schedule_poll();
}

bool nic_manager_check_poll(void)
{
    return nic_check_poll();
}
