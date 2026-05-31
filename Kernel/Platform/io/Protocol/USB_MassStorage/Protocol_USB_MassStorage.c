#include "Protocol_USB_MassStorage.h"
#include "../../IO_Main.h"
#include "Drivers/Module/DriverManager.h"
#include "Debug/serial/Serial.h"

static bool g_usb_ms_working = false;

bool usb_ms_init(uint64_t partition_lba) {
    (void)partition_lba;
    g_usb_ms_working = false;

    uint8_t probe_buf[512];
    if (!driver_manager_input_usb_read_sectors(0, probe_buf, 1)) {
        return false;
    }

    g_usb_ms_working = true;
    return true;
}

bool usb_ms_read(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    if (!g_usb_ms_working) return false;
    if (sectors == 0) return true;
    return driver_manager_input_usb_read_sectors(lba, buffer, sectors);
}

bool usb_ms_write(uint32_t lba, const uint8_t *buffer, uint32_t sectors)
{
    if (!g_usb_ms_working) return false;
    if (sectors == 0) return true;
    return driver_manager_input_usb_write_sectors(lba, buffer, sectors);
}

bool usb_ms_is_working(void) {
    return g_usb_ms_working;
}