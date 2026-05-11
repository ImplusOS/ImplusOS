#include "IO_Main.h"
#include "Protocol/ATA/Protocol_ATA.h"
#include "Protocol/USB_MassStorage/Protocol_USB_MassStorage.h"
#include "Debug/serial/Serial.h"
 
static io_protocol_type_t g_current_protocol = IO_PROTOCOL_TYPE_NONE;
 
void disk_io_init(uint64_t partition_lba, uint32_t boot_drive_type) {
    g_current_protocol = IO_PROTOCOL_TYPE_NONE;
 
    if (boot_drive_type == 1) {
        if (ata_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_ATA;
            return;
        }
    } else if (boot_drive_type == 2) {
        if (usb_ms_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
            return;
        }
    }
 
    if (g_current_protocol == IO_PROTOCOL_TYPE_NONE) {
        if (boot_drive_type == 0) {
            if (ata_init(partition_lba)) {
                g_current_protocol = IO_PROTOCOL_TYPE_ATA;
                return;
            }
            
            if (usb_ms_init(partition_lba)) {
                g_current_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
                return;
            }
        }
    }
}
 
bool disk_read(uint32_t lba, uint8_t *buffer, uint32_t sectors) {
    if (g_current_protocol == IO_PROTOCOL_TYPE_ATA) {
        return ata_read(lba, buffer, sectors);
    } else if (g_current_protocol == IO_PROTOCOL_TYPE_USB_MASS_STORAGE) {
        return usb_ms_read(lba, buffer, sectors);
    }
    return false;
}
 
bool disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors) {
    if (g_current_protocol == IO_PROTOCOL_TYPE_ATA) {
        return ata_write(lba, buffer, sectors);
    } else if (g_current_protocol == IO_PROTOCOL_TYPE_USB_MASS_STORAGE) {
        return usb_ms_write(lba, buffer, sectors);
    }
    return false;
}
 
bool disk_io_is_working(void) {
    if (g_current_protocol == IO_PROTOCOL_TYPE_ATA) {
        return ata_is_working();
    } else if (g_current_protocol == IO_PROTOCOL_TYPE_USB_MASS_STORAGE) {
        return usb_ms_is_working();
    }
    return false;
}
 
io_protocol_type_t disk_io_get_protocol(void) {
    return g_current_protocol;
}