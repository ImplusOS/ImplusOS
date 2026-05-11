#include "DriverManager.h"

#include "kernel/boot_info.h"
#include "DriverModule.h"
#include "Drivers/Client/Display/Display_Main.h"
#include "Drivers/Client/FileSystem/FAT32/FAT32_Main.h"
#include "Drivers/Client/NIC/NIC.h"
#include "Drivers/Client/PS2/PS2_Input.h"
#include "Drivers/Client/USB/USB_Driver_API.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t attached;
    driver_manager_kind_t kind;
    char module_name[LOADED_FILE_NAME_MAX];
    const void *driver_api;
} driver_manager_entry_t;

static driver_manager_entry_t g_driver_entries[MAX_LOADED_FILES];

static bool driver_manager_streq(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    return strcmp(lhs, rhs) == 0;
}

void driver_manager_init(void)
{
    driver_manager_detach_all();
}

bool driver_manager_attach(const char *module_name,
                           driver_manager_kind_t kind,
                           const void *driver_api)
{
    if (module_name == NULL || module_name[0] == '\0' || driver_api == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        driver_manager_entry_t *entry = &g_driver_entries[i];
        if (entry->attached != 0u &&
            driver_manager_streq(entry->module_name, module_name)) {
            entry->kind = kind;
            entry->driver_api = driver_api;
            return true;
        }
    }

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        driver_manager_entry_t *entry = &g_driver_entries[i];
        if (entry->attached != 0u) {
            continue;
        }

        memset(entry, 0, sizeof(*entry));
        entry->attached = 1u;
        entry->kind = kind;
        entry->driver_api = driver_api;
        strncpy(entry->module_name, module_name, sizeof(entry->module_name) - 1u);
        entry->module_name[sizeof(entry->module_name) - 1u] = '\0';
        return true;
    }

    return false;
}

bool driver_manager_detach(const char *module_name)
{
    if (module_name == NULL || module_name[0] == '\0') {
        return false;
    }

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        driver_manager_entry_t *entry = &g_driver_entries[i];
        if (entry->attached == 0u ||
            !driver_manager_streq(entry->module_name, module_name)) {
            continue;
        }

        memset(entry, 0, sizeof(*entry));
        display_driver_detached(module_name);
        return true;
    }

    return false;
}

void driver_manager_detach_all(void)
{
    memset(g_driver_entries, 0, sizeof(g_driver_entries));
}

const void *driver_manager_get_by_module_name(const char *module_name)
{
    if (module_name == NULL || module_name[0] == '\0') {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        const driver_manager_entry_t *entry = &g_driver_entries[i];
        if (entry->attached != 0u &&
            driver_manager_streq(entry->module_name, module_name)) {
            return entry->driver_api;
        }
    }

    return NULL;
}

const void *driver_manager_get_by_kind(driver_manager_kind_t kind)
{
    if (kind == DRIVER_MANAGER_KIND_UNKNOWN) {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        const driver_manager_entry_t *entry = &g_driver_entries[i];
        if (entry->attached != 0u && entry->kind == kind) {
            return entry->driver_api;
        }
    }

    return NULL;
}

const void *driver_manager_get_named(driver_manager_kind_t kind,
                                     const char *module_name)
{
    if (module_name == NULL || module_name[0] == '\0') {
        return NULL;
    }

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        const driver_manager_entry_t *entry = &g_driver_entries[i];
        if (entry->attached == 0u ||
            entry->kind != kind ||
            !driver_manager_streq(entry->module_name, module_name)) {
            continue;
        }

        return entry->driver_api;
    }

    return NULL;
}

const pci_driver_t *driver_manager_get_pci_driver(void)
{
    return (const pci_driver_t *)driver_manager_get_by_kind(DRIVER_MANAGER_KIND_PCI);
}

const fat32_driver_t *driver_manager_get_fat32_driver(void)
{
    return (const fat32_driver_t *)driver_manager_get_by_kind(DRIVER_MANAGER_KIND_FAT32);
}

const driver_input_t *driver_manager_get_ps2_driver(void)
{
    return (const driver_input_t *)driver_manager_get_named(DRIVER_MANAGER_KIND_INPUT,
                                                            "PS2_Driver.ELF");
}

const usb_master_vtable_t *driver_manager_get_usb_driver(void)
{
    return (const usb_master_vtable_t *)driver_manager_get_by_kind(DRIVER_MANAGER_KIND_USB);
}

const driver_display_t *driver_manager_get_display_driver(const char *module_name)
{
    return (const driver_display_t *)driver_manager_get_named(DRIVER_MANAGER_KIND_DISPLAY,
                                                              module_name);
}

const driver_nic_t *driver_manager_get_nic_driver(void)
{
    return (const driver_nic_t *)driver_manager_get_by_kind(DRIVER_MANAGER_KIND_NIC);
}

bool driver_manager_unload_module(const char *module_name)
{
    return driver_module_manager_unload_by_name(module_name);
}

bool driver_manager_reload_module(const char *module_name)
{
    return driver_module_manager_reload_by_name(module_name);
}

bool driver_manager_fs_init(const FAT32_BPB *initial_bpb)
{
    return fat32_init(initial_bpb);
}

bool driver_manager_fs_find_file(const char *path, FAT32_FILE *file)
{
    return fat32_find_file(path, file);
}

bool driver_manager_fs_read_file(FAT32_FILE *file, uint8_t *buffer)
{
    return fat32_read_file(file, buffer);
}

bool driver_manager_fs_write_file(FAT32_FILE *file, const uint8_t *buffer)
{
    return fat32_write_file(file, buffer);
}

bool driver_manager_fs_read_at(FAT32_FILE *file, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    return fat32_read_at(file, offset, buffer, size);
}

bool driver_manager_fs_write_at(FAT32_FILE *file, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    return fat32_write_at(file, offset, buffer, size);
}

bool driver_manager_fs_truncate(FAT32_FILE *file, uint32_t new_size)
{
    return fat32_truncate(file, new_size);
}

uint32_t driver_manager_fs_get_file_size(FAT32_FILE *file)
{
    return fat32_get_file_size(file);
}

void driver_manager_fs_list_root_files(void)
{
    fat32_list_root_files();
}

bool driver_manager_fs_creat(const char *path)
{
    return fat32_creat(path);
}

bool driver_manager_fs_mkdir(const char *path)
{
    return fat32_mkdir(path);
}

int32_t driver_manager_fs_opendir(const char *path)
{
    return fat32_opendir(path);
}

int32_t driver_manager_fs_readdir(int32_t dir_handle, FAT32_DIRENT *out_entry)
{
    return fat32_readdir(dir_handle, out_entry);
}

int32_t driver_manager_fs_closedir(int32_t dir_handle)
{
    return fat32_closedir(dir_handle);
}

bool driver_manager_fs_unlink(const char *path)
{
    return fat32_unlink(path);
}

void driver_manager_fs_set_case_sensitive_lookup(bool enabled)
{
    fat32_set_case_sensitive_lookup(enabled);
}

bool driver_manager_fs_get_case_sensitive_lookup(void)
{
    return fat32_get_case_sensitive_lookup();
}

bool driver_manager_display_init(void)
{
    return display_init();
}

bool driver_manager_display_is_ready(void)
{
    return display_is_ready();
}

uint32_t driver_manager_display_width(void)
{
    return display_width();
}

uint32_t driver_manager_display_height(void)
{
    return display_height();
}

void driver_manager_display_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    display_draw_pixel(x, y, color);
}

void driver_manager_display_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    display_fill_rect(x, y, w, h, color);
}

void driver_manager_display_present(void)
{
    display_present();
}

void *driver_manager_display_get_framebuffer(void)
{
    return display_get_framebuffer();
}

bool driver_manager_input_ps2_init(void)
{
    return ps2_input_init();
}

void driver_manager_input_ps2_poll(void)
{
    ps2_input_poll();
}

int32_t driver_manager_input_ps2_read_keyboard(driver_keyboard_event_t *out_event)
{
    return ps2_input_read_keyboard(out_event);
}

int32_t driver_manager_input_ps2_read_mouse(driver_mouse_event_t *out_event)
{
    return ps2_input_read_mouse(out_event);
}

void driver_manager_input_usb_init(void)
{
    usb_driver_client_init();
}

bool driver_manager_input_usb_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    return usb_driver_client_read_sectors(lba, buffer, sectors);
}

bool driver_manager_input_usb_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors)
{
    return usb_driver_client_write_sectors(lba, buffer, sectors);
}

int32_t driver_manager_input_usb_read_keyboard(driver_keyboard_event_t *out_event)
{
    return usb_driver_client_read_keyboard(out_event);
}

int32_t driver_manager_input_usb_read_mouse(driver_mouse_event_t *out_event)
{
    return usb_driver_client_read_mouse(out_event);
}

void driver_manager_input_usb_poll(void)
{
    usb_driver_client_poll();
}

void driver_manager_input_usb_drain_keyboard(driver_keyboard_event_t *tmp,
                                             void (*forward)(driver_keyboard_event_t *))
{
    usb_driver_client_drain_keyboard(tmp, forward);
}

void driver_manager_input_usb_drain_mouse(driver_mouse_event_t *tmp,
                                          void (*forward)(driver_mouse_event_t *))
{
    usb_driver_client_drain_mouse(tmp, forward);
}

void driver_manager_input_usb_schedule_poll(void)
{
    usb_driver_client_schedule_poll();
}

bool driver_manager_input_usb_check_poll(void)
{
    return usb_driver_client_check_poll();
}

bool driver_manager_nic_init(void)
{
    return nic_init();
}

bool driver_manager_nic_is_ready(void)
{
    return nic_is_ready();
}

uint16_t driver_manager_nic_mtu(void)
{
    return nic_mtu();
}

void driver_manager_nic_get_mac(uint8_t mac_out[6])
{
    nic_get_mac(mac_out);
}

bool driver_manager_nic_send_frame(const uint8_t *frame, uint16_t frame_len)
{
    return nic_send_frame(frame, frame_len);
}

void driver_manager_nic_poll(void)
{
    nic_poll();
}

void driver_manager_nic_set_rx_callback(driver_nic_rx_callback_t cb)
{
    nic_set_rx_callback(cb);
}

void driver_manager_nic_schedule_poll(void)
{
    nic_schedule_poll();
}

bool driver_manager_nic_check_poll(void)
{
    return nic_check_poll();
}
