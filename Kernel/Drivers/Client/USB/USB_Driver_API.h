#pragma once

#include "Drivers/Module/DriverBinary.h"

typedef void *(*usb_driver_init_fn)(const driver_binary_t *api);

void usb_driver_client_init(void);
bool usb_driver_client_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool usb_driver_client_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors);

int32_t usb_driver_client_read_keyboard(driver_keyboard_event_t *out_event);
int32_t usb_driver_client_read_mouse(driver_mouse_event_t *out_event);

void usb_driver_client_poll(void);

void usb_driver_client_drain_keyboard(driver_keyboard_event_t *tmp,
                                      void (*forward)(driver_keyboard_event_t *));
void usb_driver_client_drain_mouse(driver_mouse_event_t *tmp,
                                   void (*forward)(driver_mouse_event_t *));

void usb_driver_client_schedule_poll(void);
bool usb_driver_client_check_poll(void);
