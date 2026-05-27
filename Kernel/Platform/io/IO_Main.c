#include "IO_Main.h"
#include "Protocol/ATA/Protocol_ATA.h"
#include "Protocol/USB_MassStorage/Protocol_USB_MassStorage.h"
#include "Protocol/AHCI/Protocol_AHCI.h"
#include "Debug/serial/Serial.h"
#include "Debug/printf/printf.h"
 
static io_protocol_type_t g_current_protocol = IO_PROTOCOL_TYPE_NONE;
static uint32_t g_partition_lba = 0;
 
void disk_io_init(uint64_t partition_lba, uint32_t boot_drive_type) {
    g_current_protocol = IO_PROTOCOL_TYPE_NONE;
    g_partition_lba = (uint32_t)partition_lba;
    
    if (boot_drive_type == 1) {
        if (ata_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_ATA;
            return;
        }
    }
    else if (boot_drive_type == 2) {
        if (usb_ms_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_USB_MASS_STORAGE;
            return;
        }
    }
    if (g_current_protocol == IO_PROTOCOL_TYPE_NONE) {

        if (ahci_init(partition_lba)) {
            g_current_protocol = IO_PROTOCOL_TYPE_AHCI;
            return;
        }
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

uint32_t disk_get_partition_lba(void) {
    return g_partition_lba;
}