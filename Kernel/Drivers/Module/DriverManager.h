#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "DriverBinary.h"
#include "Core/vfs/VFS.h"
#include "Drivers/Client/FileSystem/ISO9660/ISO9660_Main.h"
#include "Drivers/Client/PCI/PCI_Main.h"

typedef enum {
    DRIVER_MANAGER_KIND_UNKNOWN = 0,
    DRIVER_MANAGER_KIND_PCI,
    DRIVER_MANAGER_KIND_FAT32,
    DRIVER_MANAGER_KIND_ISO9660,
    DRIVER_MANAGER_KIND_DISPLAY,
    DRIVER_MANAGER_KIND_INPUT,
    DRIVER_MANAGER_KIND_USB,
    DRIVER_MANAGER_KIND_NIC,
} driver_manager_kind_t;

void driver_manager_init(void);

bool driver_manager_attach(const char *module_name,
                           driver_manager_kind_t kind,
                           const void *driver_api);
bool driver_manager_detach(const char *module_name);
void driver_manager_detach_all(void);

const void *driver_manager_get_by_module_name(const char *module_name);
const void *driver_manager_get_by_kind(driver_manager_kind_t kind);
const void *driver_manager_get_named(driver_manager_kind_t kind,
                                     const char *module_name);

const pci_driver_t *driver_manager_get_pci_driver(void);
const iso9660_driver_t *driver_manager_get_iso9660_driver(void);
const driver_input_t *driver_manager_get_ps2_driver(void);
const usb_master_vtable_t *driver_manager_get_usb_driver(void);
const driver_display_t *driver_manager_get_display_driver(const char *module_name);
const driver_nic_t *driver_manager_get_nic_driver(void);

bool driver_manager_unload_module(const char *module_name);
bool driver_manager_reload_module(const char *module_name);

bool driver_manager_fs_init(void);
bool driver_manager_fs_find_file(const char *path, vfs_file_t *file);
bool driver_manager_fs_read_file(vfs_file_t *file, uint8_t *buffer);
bool driver_manager_fs_write_file(vfs_file_t *file, const uint8_t *buffer);
bool driver_manager_fs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size);
bool driver_manager_fs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size);
bool driver_manager_fs_truncate(vfs_file_t *file, uint32_t new_size);
uint32_t driver_manager_fs_get_file_size(vfs_file_t *file);
void driver_manager_fs_list_root_files(void);
bool driver_manager_fs_creat(const char *path);
bool driver_manager_fs_mkdir(const char *path);
int32_t driver_manager_fs_opendir(const char *path);
int32_t driver_manager_fs_readdir(int32_t dir_handle, vfs_dirent_t *out_entry);
int32_t driver_manager_fs_closedir(int32_t dir_handle);
bool driver_manager_fs_unlink(const char *path);
void driver_manager_fs_set_case_sensitive_lookup(bool enabled);
bool driver_manager_fs_get_case_sensitive_lookup(void);

bool driver_manager_display_init(void);
bool driver_manager_display_is_ready(void);
uint32_t driver_manager_display_width(void);
uint32_t driver_manager_display_height(void);
void driver_manager_display_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t driver_manager_display_get_pixel(uint32_t x, uint32_t y);
void driver_manager_display_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void driver_manager_display_present(void);
void *driver_manager_display_get_framebuffer(void);

bool driver_manager_input_ps2_init(void);
void driver_manager_input_ps2_poll(void);
int32_t driver_manager_input_ps2_read_keyboard(driver_keyboard_event_t *out_event);
int32_t driver_manager_input_ps2_read_mouse(driver_mouse_event_t *out_event);

void driver_manager_input_usb_init(void);
bool driver_manager_input_usb_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors);
bool driver_manager_input_usb_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
int32_t driver_manager_input_usb_read_keyboard(driver_keyboard_event_t *out_event);
int32_t driver_manager_input_usb_read_mouse(driver_mouse_event_t *out_event);
void driver_manager_input_usb_poll(void);
void driver_manager_input_usb_drain_keyboard(driver_keyboard_event_t *tmp,
                                             void (*forward)(driver_keyboard_event_t *));
void driver_manager_input_usb_drain_mouse(driver_mouse_event_t *tmp,
                                          void (*forward)(driver_mouse_event_t *));
void driver_manager_input_usb_schedule_poll(void);
bool driver_manager_input_usb_check_poll(void);

bool driver_manager_nic_init(void);
bool driver_manager_nic_is_ready(void);
uint16_t driver_manager_nic_mtu(void);
void driver_manager_nic_get_mac(uint8_t mac_out[6]);
bool driver_manager_nic_send_frame(const uint8_t *frame, uint16_t frame_len);
void driver_manager_nic_poll(void);
void driver_manager_nic_set_rx_callback(driver_nic_rx_callback_t cb);
void driver_manager_nic_schedule_poll(void);
bool driver_manager_nic_check_poll(void);
