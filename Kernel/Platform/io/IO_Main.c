#include "IO_Main.h"
#include "Protocol/ATA/Protocol_ATA.h"
#include "Protocol/USB_MassStorage/Protocol_USB_MassStorage.h"
#include "Protocol/AHCI/Protocol_AHCI.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"
 
static io_protocol_type_t g_current_protocol = IO_PROTOCOL_TYPE_NONE;
 
void disk_io_init(uint64_t partition_lba, uint32_t boot_drive_type) {
    g_current_protocol = IO_PROTOCOL_TYPE_NONE;

    serial_write_string("[disk] disk_io_init start\n");

    if (boot_drive_type == 1) {
        serial_write_string("[disk] trying ATA boot device\n");

        if (ata_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_ATA;
            serial_write_string("[disk] ATA initialized\n");
            return;
        }

        serial_write_string("[disk] ATA init failed\n");
    }
    else if (boot_drive_type == 2) {
        serial_write_string("[disk] trying USB mass storage boot device\n");

        if (usb_ms_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
            serial_write_string("[disk] USB mass storage initialized\n");
            return;
        }

        serial_write_string("[disk] USB mass storage init failed\n");
    }

    if (g_current_protocol == IO_PROTOCOL_TYPE_NONE) {

        serial_write_string("[disk] fallback probing start\n");

        if (ahci_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_AHCI;
            serial_write_string("[disk] AHCI initialized\n");
            return;
        }

        serial_write_string("[disk] AHCI init failed\n");

        if (ata_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_ATA;
            serial_write_string("[disk] ATA initialized (fallback)\n");
            return;
        }

        serial_write_string("[disk] ATA fallback failed\n");

        if (usb_ms_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
            serial_write_string("[disk] USB initialized (fallback)\n");
            return;
        }

        serial_write_string("[disk] USB fallback failed\n");
    }

    serial_write_string("[disk] disk_io_init end\n");
}
 
bool disk_read(uint32_t lba, uint8_t *buffer, uint32_t sectors) {
    if (g_current_protocol == IO_PROTOCOL_TYPE_ATA)
        return ata_read(lba, buffer, sectors);
    if (g_current_protocol == IO_PROTOCOL_TYPE_AHCI)
        return ahci_read(lba, buffer, sectors);
    if (g_current_protocol == IO_PROTOCOL_TYPE_USB_MASS_STORAGE)
        return usb_ms_read(lba, buffer, sectors);
    return false;
}

bool disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    if (g_current_protocol == IO_PROTOCOL_TYPE_ATA)
        return ata_write(lba, buffer, sectors);
    if (g_current_protocol == IO_PROTOCOL_TYPE_AHCI)
        return ahci_write(lba, buffer, sectors);
    if (g_current_protocol == IO_PROTOCOL_TYPE_USB_MASS_STORAGE)
        return usb_ms_write(lba, buffer, sectors);
    return false;
}

bool disk_io_is_working(void) {
    if (g_current_protocol == IO_PROTOCOL_TYPE_ATA)
        return ata_is_working();
    if (g_current_protocol == IO_PROTOCOL_TYPE_AHCI)
        return ahci_is_working();
    if (g_current_protocol == IO_PROTOCOL_TYPE_USB_MASS_STORAGE)
        return usb_ms_is_working();
    return false;
}
 
io_protocol_type_t disk_io_get_protocol(void) {
    return g_current_protocol;
}