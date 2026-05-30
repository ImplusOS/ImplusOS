#include "IO_Main.h"
#include "Protocol/ATA/Protocol_ATA.h"
#include "Protocol/USB_MassStorage/Protocol_USB_MassStorage.h"
#include "Protocol/AHCI/Protocol_AHCI.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"
 
typedef struct {
    const char *name;
    io_protocol_type_t protocol;
    bool (*init)(uint64_t partition_lba);
    bool (*read)(uint32_t lba, uint8_t *buffer, uint32_t sectors);
    bool (*write)(uint32_t lba, const uint8_t *buffer, uint32_t sectors);
    bool (*is_working)(void);
} block_device_t;

static const block_device_t g_block_devices[] = {
    { "AHCI", IO_PROTOCOL_TYPE_AHCI, ahci_init, ahci_read, ahci_write, ahci_is_working },
    { "ATA", IO_PROTOCOL_TYPE_ATA, ata_init, ata_read, ata_write, ata_is_working },
    { "USB_MASS_STORAGE", IO_PROTOCOL_TYPE_USB_MASS_STORAGE, usb_ms_init, usb_ms_read, usb_ms_write, usb_ms_is_working },
};

static const block_device_t *g_current_block_device = NULL;
static io_protocol_type_t g_current_protocol = IO_PROTOCOL_TYPE_NONE;
static uint32_t g_partition_lba = 0;

static const block_device_t *block_device_find_by_protocol(io_protocol_type_t protocol) {
    for (size_t i = 0; i < sizeof(g_block_devices) / sizeof(g_block_devices[0]); ++i) {
        if (g_block_devices[i].protocol == protocol) {
            return &g_block_devices[i];
        }
    }
    return NULL;
}

static const block_device_t *block_device_select_probe(uint64_t partition_lba) {
    for (size_t i = 0; i < sizeof(g_block_devices) / sizeof(g_block_devices[0]); ++i) {
        const block_device_t *device = &g_block_devices[i];
        if (device->init && device->init(partition_lba)) {
            return device;
        }
    }
    return NULL;
}

static const block_device_t *block_device_select(io_protocol_type_t requested_protocol,
                                                uint64_t partition_lba) {
    if (requested_protocol != IO_PROTOCOL_TYPE_NONE) {
        const block_device_t *device = block_device_find_by_protocol(requested_protocol);
        if (device && device->init && device->init(partition_lba)) {
            return device;
        }
    }
    return block_device_select_probe(partition_lba);
}

void disk_io_init(uint64_t partition_lba, uint32_t boot_drive_type) {
    g_current_protocol = IO_PROTOCOL_TYPE_NONE;
    g_partition_lba = (uint32_t)partition_lba;
    g_current_block_device = NULL;

    io_protocol_type_t requested_protocol = IO_PROTOCOL_TYPE_NONE;
    if (boot_drive_type == 1) {
        requested_protocol = IO_PROTOCOL_TYPE_ATA;
    } else if (boot_drive_type == 2) {
        requested_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
    }

    const block_device_t *device = block_device_select(requested_protocol, partition_lba);
    if (device) {
        g_current_block_device = device;
        g_current_protocol = device->protocol;
    }
}
 
bool disk_read(uint32_t lba, uint8_t *buffer, uint32_t sectors) {
    if (g_current_block_device && g_current_block_device->read) {
        return g_current_block_device->read(lba, buffer, sectors);
    }
    return false;
}

bool disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    if (g_current_block_device && g_current_block_device->write) {
        return g_current_block_device->write(lba, buffer, sectors);
    }
    return false;
}

bool disk_io_is_working(void) {
    if (g_current_block_device && g_current_block_device->is_working) {
        return g_current_block_device->is_working();
    }
    return false;
}
 
io_protocol_type_t disk_io_get_protocol(void) {
    return g_current_protocol;
}

uint32_t disk_get_partition_lba(void) {
    return g_partition_lba;
}