#include "DriverManager.h"

#include "BlockManager.h"
#include "DeviceRegistry.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "NicManager.h"
#include "kernel/boot_info.h"
#include "DriverModule.h"
#include "Debug/serial/Serial.h"
#include "Drivers/Client/Display/Display_Main.h"
#include "Core/vfs/VFS.h"
#include "Drivers/Client/NIC/NIC.h"
#include "Drivers/Client/PS2/PS2_Input.h"

#include <stddef.h>
#include <string.h>

static bool driver_manager_streq(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    return strcmp(lhs, rhs) == 0;
}

static void driver_manager_device_detached(const char *name, device_type_t type)
{
    if (type == DEVICE_TYPE_DISPLAY) {
        display_manager_on_device_detached(name);
    }
}

void driver_manager_init(void)
{
    device_registry_init();
    device_registry_set_detach_callback(driver_manager_device_detached);
}

bool driver_manager_attach(const char *module_name,
                           driver_manager_kind_t kind,
                           const void *driver_api)
{
    device_t dev;

    if (module_name == NULL || module_name[0] == '\0' || driver_api == NULL) {
        return false;
    }

    dev.type = kind;
    dev.ops = driver_api;
    dev.priv = NULL;
    dev.name = module_name;
    return device_registry_add(&dev);
}

bool driver_manager_detach(const char *module_name)
{
    return device_registry_remove(module_name);
}

void driver_manager_detach_all(void)
{
    device_registry_clear();
}

const void *driver_manager_get_by_module_name(const char *module_name)
{
    if (module_name == NULL || module_name[0] == '\0') {
        return NULL;
    }

    for (device_type_t type = DEVICE_TYPE_PCI; type <= DEVICE_TYPE_FILESYSTEM; ++type) {
        for (uint32_t index = 0;; ++index) {
            const device_t *dev = device_registry_find_by_index(type, index);
            if (dev == NULL) {
                break;
            }
            if (driver_manager_streq(dev->name, module_name)) {
                return dev->ops;
            }
        }
    }

    return NULL;
}

static const device_t *driver_manager_get_device_by_kind(driver_manager_kind_t kind)
{
    return device_registry_find(kind, NULL);
}

static const device_t *driver_manager_get_device_named(driver_manager_kind_t kind,
                                                       const char *module_name)
{
    if (module_name == NULL || module_name[0] == '\0') {
        return NULL;
    }

    return device_registry_find(kind, module_name);
}

const device_t *driver_manager_find(driver_manager_kind_t kind,
                                    const char *module_name)
{
    if (module_name == NULL || module_name[0] == '\0') {
        return driver_manager_get_device_by_kind(kind);
    }
    return driver_manager_get_device_named(kind, module_name);
}

const device_t *device_manager_find(device_type_t type,
                                   const char *module_name)
{
    return driver_manager_find((driver_manager_kind_t)type, module_name);
}

const void *driver_manager_get_by_kind(driver_manager_kind_t kind)
{
    const device_t *device = driver_manager_get_device_by_kind(kind);
    return device ? device->ops : NULL;
}

const void *driver_manager_get_named(driver_manager_kind_t kind,
                                     const char *module_name)
{
    const device_t *device = driver_manager_get_device_named(kind, module_name);
    return device ? device->ops : NULL;
}

const pci_driver_t *driver_manager_get_pci_driver(void)
{
    return (const pci_driver_t *)driver_manager_get_by_kind(DEVICE_TYPE_PCI);
}

const iso9660_driver_t *driver_manager_get_iso9660_driver(void)
{
    return (const iso9660_driver_t *)driver_manager_get_named(DEVICE_TYPE_FILESYSTEM,
                                                              "ISO9660_Driver.ELF");
}

const driver_input_t *driver_manager_get_ps2_driver(void)
{
    return (const driver_input_t *)driver_manager_get_named(DEVICE_TYPE_INPUT,
                                                            "PS2_Driver.ELF");
}

const usb_master_vtable_t *driver_manager_get_usb_driver(void)
{
    return (const usb_master_vtable_t *)driver_manager_get_by_kind(DEVICE_TYPE_USB);
}

const driver_display_t *driver_manager_get_display_driver(const char *module_name)
{
    const device_t *device = driver_manager_find(DEVICE_TYPE_DISPLAY,
                                                 module_name);
    return device ? (const driver_display_t *)device->ops : NULL;
}

const driver_nic_t *driver_manager_get_nic_driver(void)
{
    return (const driver_nic_t *)driver_manager_get_by_kind(DEVICE_TYPE_NIC);
}

bool driver_manager_unload_module(const char *module_name)
{
    return driver_module_manager_unload_by_name(module_name);
}

bool driver_manager_reload_module(const char *module_name)
{
    return driver_module_manager_reload_by_name(module_name);
}

bool driver_manager_fs_init(void)
{
    return vfs_init();
}

bool driver_manager_fs_find_file(const char *path, vfs_file_t *file)
{
    return vfs_find_file(path, file);
}

bool driver_manager_fs_read_file(vfs_file_t *file, uint8_t *buffer)
{
    return vfs_read_file(file, buffer);
}

bool driver_manager_fs_write_file(vfs_file_t *file, const uint8_t *buffer)
{
    return vfs_write_file(file, buffer);
}

bool driver_manager_fs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    return vfs_read_at(file, offset, buffer, size);
}

bool driver_manager_fs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    return vfs_write_at(file, offset, buffer, size);
}

bool driver_manager_fs_truncate(vfs_file_t *file, uint32_t new_size)
{
    return vfs_truncate(file, new_size);
}

uint32_t driver_manager_fs_get_file_size(vfs_file_t *file)
{
    return vfs_get_file_size(file);
}

bool driver_manager_fs_close_file(vfs_file_t *file)
{
    return vfs_close_file(file);
}

void driver_manager_fs_list_root_files(void)
{
    vfs_list_root();
}

bool driver_manager_fs_creat(const char *path)
{
    return vfs_creat(path);
}

bool driver_manager_fs_mkdir(const char *path)
{
    return vfs_mkdir(path);
}

int32_t driver_manager_fs_opendir(const char *path)
{
    return vfs_opendir(path);
}

int32_t driver_manager_fs_readdir(int32_t dir_handle, vfs_dirent_t *out_entry)
{
    return vfs_readdir(dir_handle, out_entry);
}

int32_t driver_manager_fs_closedir(int32_t dir_handle)
{
    return vfs_closedir(dir_handle);
}

bool driver_manager_fs_unlink(const char *path)
{
    return vfs_unlink(path);
}

void driver_manager_fs_set_case_sensitive_lookup(bool enabled)
{
    vfs_set_case_sensitive(enabled);
}

bool driver_manager_fs_get_case_sensitive_lookup(void)
{
    return vfs_get_case_sensitive();
}

bool driver_manager_display_init(void)
{
    return display_manager_init();
}

bool driver_manager_display_is_ready(void)
{
    return display_manager_is_ready();
}

uint32_t driver_manager_display_width(void)
{
    return display_manager_width();
}

uint32_t driver_manager_display_height(void)
{
    return display_manager_height();
}

void driver_manager_display_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    display_manager_draw_pixel(x, y, color);
}

uint32_t driver_manager_display_get_pixel(uint32_t x, uint32_t y)
{
    return display_manager_get_pixel(x, y);
}

void driver_manager_display_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    display_manager_fill_rect(x, y, w, h, color);
}

void driver_manager_display_present(void)
{
    display_manager_present();
}

void *driver_manager_display_get_framebuffer(void)
{
    return display_manager_get_framebuffer();
}

bool driver_manager_input_ps2_init(void)
{
    input_manager_init();
    return true;
}

void driver_manager_input_ps2_poll(void)
{
    input_manager_poll();
}

int32_t driver_manager_input_ps2_read_keyboard(driver_keyboard_event_t *out_event)
{
    return input_manager_read_keyboard(out_event);
}

int32_t driver_manager_input_ps2_read_mouse(driver_mouse_event_t *out_event)
{
    return input_manager_read_mouse(out_event);
}

void driver_manager_input_usb_init(void)
{
    input_manager_init();
}

bool driver_manager_input_usb_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    return block_manager_read_sectors(lba, buffer, sectors);
}

bool driver_manager_input_usb_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors)
{
    return block_manager_write_sectors(lba, buffer, sectors);
}

int32_t driver_manager_input_usb_read_keyboard(driver_keyboard_event_t *out_event)
{
    return input_manager_read_keyboard(out_event);
}

int32_t driver_manager_input_usb_read_mouse(driver_mouse_event_t *out_event)
{
    return input_manager_read_mouse(out_event);
}

void driver_manager_input_usb_poll(void)
{
    input_manager_poll();
}

void driver_manager_input_usb_drain_keyboard(driver_keyboard_event_t *tmp,
                                             void (*forward)(driver_keyboard_event_t *))
{
    input_manager_drain_keyboard(tmp, forward);
}

void driver_manager_input_usb_drain_mouse(driver_mouse_event_t *tmp,
                                          void (*forward)(driver_mouse_event_t *))
{
    input_manager_drain_mouse(tmp, forward);
}

void driver_manager_input_usb_schedule_poll(void)
{
    input_manager_schedule_poll();
}

bool driver_manager_input_usb_check_poll(void)
{
    return input_manager_check_poll();
}

bool driver_manager_nic_init(void)
{
    return nic_manager_init();
}

bool driver_manager_nic_is_ready(void)
{
    return nic_manager_is_ready();
}

uint16_t driver_manager_nic_mtu(void)
{
    return nic_manager_mtu();
}

void driver_manager_nic_get_mac(uint8_t mac_out[6])
{
    nic_manager_get_mac(mac_out);
}

bool driver_manager_nic_send_frame(const uint8_t *frame, uint16_t frame_len)
{
    return nic_manager_send_frame(frame, frame_len);
}

void driver_manager_nic_poll(void)
{
    nic_manager_poll();
}

void driver_manager_nic_set_rx_callback(driver_nic_rx_callback_t cb)
{
    nic_manager_set_rx_callback(cb);
}

void driver_manager_nic_schedule_poll(void)
{
    nic_manager_schedule_poll();
}

bool driver_manager_nic_check_poll(void)
{
    return nic_manager_check_poll();
}
