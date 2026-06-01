#pragma once

#include "DriverBinary.h"

#include <stdbool.h>
#include <stdint.h>

bool nic_manager_init(void);
bool nic_manager_is_ready(void);
uint16_t nic_manager_mtu(void);
void nic_manager_get_mac(uint8_t mac_out[6]);
bool nic_manager_send_frame(const uint8_t *frame, uint16_t frame_len);
void nic_manager_poll(void);
void nic_manager_set_rx_callback(driver_nic_rx_callback_t cb);
void nic_manager_schedule_poll(void);
bool nic_manager_check_poll(void);
