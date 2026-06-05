#include "IO_Main.h"
#include "Protocol/ATA/Protocol_ATA.h"
#include "Protocol/USB_MassStorage/Protocol_USB_MassStorage.h"
#include "Protocol/AHCI/Protocol_AHCI.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"
#include <stddef.h>
#include <string.h>

static const block_device_t g_block_devices[] = {
    { "ahci", "AHCI/ATAPI device", IO_PROTOCOL_TYPE_AHCI, ahci_init, ahci_read, ahci_write, ahci_is_working, ahci_get_device_count, ahci_select_device, ahci_get_total_bytes },
    { "ata",  "ATA disk",          IO_PROTOCOL_TYPE_ATA,  ata_init,  ata_read,  ata_write,  ata_is_working,  ata_get_device_count,  ata_select_device,  ata_get_total_bytes  },
    { "usb",  "USB mass storage",  IO_PROTOCOL_TYPE_USB_MASS_STORAGE, usb_ms_init, usb_ms_read, usb_ms_write, usb_ms_is_working, usb_ms_get_device_count, usb_ms_select_device, usb_ms_get_total_bytes },
};

#define IO_MAX_DISKS 16

static const block_device_t *g_current_block_device = NULL;
static io_protocol_type_t    g_current_protocol     = IO_PROTOCOL_TYPE_NONE;
static uint32_t              g_current_device_index = 0;
static uint32_t              g_partition_lba        = 0;
static const block_device_t *g_detected_disks[IO_MAX_DISKS];
static uint32_t              g_detected_disks_indices[IO_MAX_DISKS];
static uint32_t              g_detected_disk_count  = 0;
static bool                  g_disk_scan_done       = false;

static void copy_string(char *dst, uint32_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    uint32_t i = 0;
    if (src) {
        for (; i + 1 < dst_size && src[i]; ++i)
            dst[i] = src[i];
    }
    dst[i] = '\0';
}

static const block_device_t *block_device_find_by_protocol(io_protocol_type_t protocol) {
    for (size_t i = 0; i < sizeof(g_block_devices) / sizeof(g_block_devices[0]); ++i) {
        if (g_block_devices[i].protocol == protocol)
            return &g_block_devices[i];
    }
    return NULL;
}

static bool check_bootable_signature(const block_device_t *device, uint64_t partition_lba) {
    uint8_t buffer[2048];
    if (!device || !device->read) return false;
    
    
    if (device->read((uint32_t)partition_lba + 64, buffer, 4)) {
        if (memcmp(buffer + 1, "CD001", 5) == 0) return true;
    }
    
    
    if (device->read((uint32_t)partition_lba, buffer, 1)) {
        if (memcmp(buffer + 82, "FAT32   ", 8) == 0) return true;
        if (memcmp(buffer + 54, "FAT16   ", 8) == 0) return true;
        if (memcmp(buffer + 54, "FAT12   ", 8) == 0) return true;
    }
    
    if (partition_lba != 0) {
        if (device->read(64, buffer, 4)) {
            if (memcmp(buffer + 1, "CD001", 5) == 0) return true;
        }
        if (device->read(0, buffer, 1)) {
            if (memcmp(buffer + 82, "FAT32   ", 8) == 0) return true;
        }
    }
    
    return false;
}

static const block_device_t *block_device_probe_one(
        const block_device_t *device,
        uint64_t              partition_lba,
        uint32_t             *out_device_index)
{
    const block_device_t *saved_device    = g_current_block_device;
    io_protocol_type_t    saved_protocol  = g_current_protocol;
    uint32_t              saved_lba       = g_partition_lba;
    uint32_t              saved_dev_index  = g_current_device_index;

    if (!device || !device->init) {
        return NULL;
    }
    if (!device->init(0)) {
        return NULL;
    }
        
    uint32_t dev_count = 1;
    if (device->get_device_count) {
        dev_count = device->get_device_count();
    }
    
    for (uint32_t d = 0; d < dev_count; ++d) {
        if (device->select_device)
            device->select_device(d);

        if (check_bootable_signature(device, partition_lba)) {
            if (out_device_index) *out_device_index = d;
            if (saved_device) {
                g_current_block_device = saved_device;
                g_current_protocol     = saved_protocol;
                g_partition_lba        = saved_lba;
                g_current_device_index = saved_dev_index;
                if (saved_device->select_device)
                    saved_device->select_device(saved_dev_index);
            }
            return device;
        }
    }

    if (saved_device) {
        g_current_block_device = saved_device;
        g_current_protocol     = saved_protocol;
        g_partition_lba        = saved_lba;
        g_current_device_index = saved_dev_index;
        if (saved_device->select_device)
            saved_device->select_device(saved_dev_index);
    }
    return NULL;
}

static const block_device_t *block_device_select_probe(
        uint64_t  partition_lba,
        uint32_t *out_device_index)
{
    for (size_t i = 0; i < sizeof(g_block_devices) / sizeof(g_block_devices[0]); ++i) {
        const block_device_t *found =
            block_device_probe_one(&g_block_devices[i], partition_lba, out_device_index);
        if (found) return found;
    }
    return NULL;
}

static const block_device_t *block_device_select(
        io_protocol_type_t requested_protocol,
        uint64_t           partition_lba,
        uint32_t          *out_device_index)
{
    if (requested_protocol != IO_PROTOCOL_TYPE_NONE) {
        const block_device_t *device = block_device_find_by_protocol(requested_protocol);
        if (!device) {
            return block_device_select_probe(partition_lba, out_device_index);
        }

        const block_device_t *found =
            block_device_probe_one(device, partition_lba, out_device_index);
        if (found) return found;

        if (device->init && device->init(0)) {
            if (out_device_index) *out_device_index = 0;
            return device;
        }

        return block_device_select_probe(partition_lba, out_device_index);
    }
    return block_device_select_probe(partition_lba, out_device_index);
}

static void apply_boot_device(const block_device_t *device,
                               uint32_t              dev_idx,
                               uint32_t              partition_lba)
{
    g_current_block_device = device;
    g_current_protocol     = device->protocol;
    g_current_device_index = dev_idx;
    g_partition_lba        = partition_lba;
    if (device->select_device)
        device->select_device(dev_idx);
}

void disk_io_init(uint64_t partition_lba, uint32_t boot_drive_type) {
    serial_write_string("boot_drive_type=");
serial_write_uint32(boot_drive_type);
serial_write_string("\n");
    g_current_protocol     = IO_PROTOCOL_TYPE_NONE;
    g_partition_lba        = (uint32_t)partition_lba;
    g_current_block_device = NULL;
    g_current_device_index = 0;

    io_protocol_type_t requested_protocol = IO_PROTOCOL_TYPE_NONE;
    if (boot_drive_type == 1)
        requested_protocol = IO_PROTOCOL_TYPE_ATA;
    else if (boot_drive_type == 2)
        requested_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
    uint32_t found_index = 0;
    const block_device_t *device =
        block_device_select(requested_protocol, partition_lba, &found_index);

    if (!device) {
        for (size_t i = 0; i < sizeof(g_block_devices) / sizeof(g_block_devices[0]); ++i) {
            const block_device_t *fb = &g_block_devices[i];
            if (fb->init && fb->init(0)) {
                device      = fb;
                found_index = 0;
                break;
            }
        }
    }

    if (device) {
        apply_boot_device(device, found_index, (uint32_t)partition_lba);

        if (device->select_device) {
            device->select_device(found_index);
        }
    }
}

bool disk_read(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    serial_write_string("device=");
serial_write_string(g_current_block_device->name);
serial_write_string("\n");

serial_write_string("protocol=");
serial_write_uint32(g_current_protocol);
serial_write_string("\n");
    serial_write_string("disk_read lba=");
    serial_write_uint32(lba);

    serial_write_string(" count=");
    serial_write_uint32(sectors);

    serial_write_string(" dev=");
    serial_write_uint32(g_current_device_index);

    serial_write_string("\n");

    if (!g_current_block_device) {
        serial_write_string("NO DEVICE\n");
        return false;
    }

    if (g_current_block_device->select_device) {
        g_current_block_device->select_device(
            g_current_device_index
        );
    }
    if (g_current_block_device->read)
        return g_current_block_device->read(lba, buffer, sectors);
    serial_write_string("device ptr=");
    serial_write_uint64((uint64_t)g_current_block_device);

    serial_write_string(" read ptr=");
    serial_write_uint64((uint64_t)g_current_block_device->read);
    serial_write_string("\n");
    return false;
}

bool disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    if (!g_current_block_device) return false;
    if (g_current_block_device->select_device) {
        g_current_block_device->select_device(
            g_current_device_index
        );
    }
    if (g_current_block_device->write)
        return g_current_block_device->write(lba, buffer, sectors);
    return false;
}

bool disk_io_is_working(void) {
    if (g_current_block_device && g_current_block_device->is_working)
        return g_current_block_device->is_working();
    return false;
}

io_protocol_type_t disk_io_get_protocol(void) {
    return g_current_protocol;
}

uint32_t disk_get_partition_lba(void) {
    return g_partition_lba;
}

static void disk_scan_devices(void) {
    if (g_disk_scan_done) return;

    const block_device_t *saved_device     = g_current_block_device;
    io_protocol_type_t    saved_protocol   = g_current_protocol;
    uint32_t              saved_lba        = g_partition_lba;
    uint32_t              saved_dev_index  = g_current_device_index;

    g_detected_disk_count = 0;

    for (size_t i = 0; i < sizeof(g_block_devices) / sizeof(g_block_devices[0]); ++i) {
        const block_device_t *device = &g_block_devices[i];
        if (!device->init || !device->init(0)) continue;

        uint32_t dev_count = 1;
        if (device->get_device_count)
            dev_count = device->get_device_count();

        for (uint32_t d = 0; d < dev_count; ++d) {
            if (g_detected_disk_count < IO_MAX_DISKS) {
                g_detected_disks[g_detected_disk_count]         = device;
                g_detected_disks_indices[g_detected_disk_count] = d;
                g_detected_disk_count++;
            }
        }
    }

    if (saved_device) {
        g_current_block_device = saved_device;
        g_current_protocol     = saved_protocol;
        g_partition_lba        = saved_lba;
        g_current_device_index = saved_dev_index;
        if (saved_device->init)          saved_device->init(saved_lba);
        if (saved_device->select_device) saved_device->select_device(saved_dev_index);
    }

    g_disk_scan_done = true;
}

uint32_t disk_get_count(void) {
    disk_scan_devices();
    return g_detected_disk_count;
}

bool disk_get_info(uint32_t index, io_disk_info_t *out_info) {
    if (!out_info) return false;
    disk_scan_devices();
    if (index >= g_detected_disk_count) return false;

    const block_device_t *device  = g_detected_disks[index];
    uint32_t              dev_idx = g_detected_disks_indices[index];

    if (device->select_device)
        device->select_device(dev_idx);

    copy_string(out_info->disk_name, sizeof(out_info->disk_name), device->name);
    uint32_t len = 0;
    while (out_info->disk_name[len]) len++;
    if (len < sizeof(out_info->disk_name) - 1) {
        out_info->disk_name[len]   = (char)('0' + dev_idx);
        out_info->disk_name[len+1] = '\0';
    }

    copy_string(out_info->manufacturer, sizeof(out_info->manufacturer), "ImplusOS");
    copy_string(out_info->model,        sizeof(out_info->model),        device->model);
    out_info->protocol    = device->protocol;
    out_info->total_bytes = device->get_total_bytes ? device->get_total_bytes() : 0;
    out_info->sector_size = 512;
    out_info->flags       = 0;

    if (device == g_current_block_device && dev_idx == g_current_device_index)
        out_info->flags |= IO_DISK_FLAG_BOOT;

    if (device->write)
        out_info->flags |= IO_DISK_FLAG_WRITABLE;

    if (g_current_block_device) {
        if (g_current_block_device->select_device)
            g_current_block_device->select_device(g_current_device_index);
    }

    return true;
}

static bool disk_raw_io(uint32_t       index,
                        uint32_t       lba,
                        uint8_t       *read_buffer,
                        const uint8_t *write_buffer,
                        uint32_t       sectors)
{
    if (sectors == 0)                                    return true;
    if ((read_buffer == NULL) == (write_buffer == NULL)) return false;

    disk_scan_devices();
    if (index >= g_detected_disk_count) return false;

    const block_device_t *saved_device    = g_current_block_device;
    io_protocol_type_t    saved_protocol  = g_current_protocol;
    uint32_t              saved_lba       = g_partition_lba;
    uint32_t              saved_dev_index = g_current_device_index;

    const block_device_t *device  = g_detected_disks[index];
    uint32_t              dev_idx = g_detected_disks_indices[index];
    bool ok = false;

    if (device->select_device)
        device->select_device(dev_idx);

    if (device->init && device->init(0)) {
        if (read_buffer  && device->read)  ok = device->read (lba, read_buffer,  sectors);
        else if (write_buffer && device->write) ok = device->write(lba, write_buffer, sectors);
    }
    
    if (saved_device) {
        g_current_block_device = saved_device;
        g_current_protocol     = saved_protocol;
        g_partition_lba        = saved_lba;
        g_current_device_index = saved_dev_index;
        if (saved_device->init)          saved_device->init(saved_lba);
        if (saved_device->select_device) saved_device->select_device(saved_dev_index);
    }

    return ok;
}

bool disk_raw_read(uint32_t index, uint32_t lba, uint8_t *buffer, uint32_t sectors) {
    return disk_raw_io(index, lba, buffer, NULL, sectors);
}

bool disk_raw_write(uint32_t index, uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    return disk_raw_io(index, lba, NULL, buffer, sectors);
}