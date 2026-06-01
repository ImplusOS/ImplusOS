#include "BlockManager.h"

#include "DeviceRegistry.h"
#include "DriverBinary.h"

static const driver_storage_t *block_manager_get_storage(void)
{
    const device_t *dev = device_registry_find(DEVICE_TYPE_BLOCK, NULL);
    if (dev != 0) {
        return (const driver_storage_t *)dev->ops;
    }

    dev = device_registry_find(DEVICE_TYPE_USB, NULL);
    if (dev != 0) {
        const usb_master_vtable_t *usb = (const usb_master_vtable_t *)dev->ops;
        if (usb != 0) {
            return &usb->storage;
        }
    }

    return 0;
}

bool block_manager_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t sectors)
{
    const driver_storage_t *storage = block_manager_get_storage();
    if (storage == 0 || storage->read_sectors == 0) {
        return false;
    }
    return storage->read_sectors(lba, buffer, sectors);
}

bool block_manager_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t sectors)
{
    const driver_storage_t *storage = block_manager_get_storage();
    if (storage == 0 || storage->write_sectors == 0) {
        return false;
    }
    return storage->write_sectors(lba, buffer, sectors);
}
