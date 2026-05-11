#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*nic_rx_callback_t)(const uint8_t *frame, uint16_t frame_len);

bool nic_init(void);
bool nic_is_ready(void);
uint16_t nic_mtu(void);
void nic_get_mac(uint8_t mac_out[6]);

bool nic_send_frame(const uint8_t *frame, uint16_t frame_len);
void nic_poll(void);

void nic_set_rx_callback(nic_rx_callback_t cb);

void nic_schedule_poll(void);
bool nic_check_poll(void);
