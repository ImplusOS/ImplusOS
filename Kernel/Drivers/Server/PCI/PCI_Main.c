#include "Drivers/Client/PCI/PCI_Main.h"

#include <stdint.h>
#include <stddef.h>

#ifdef IMPLUS_DRIVER_MODULE
#include "Drivers/Module/DriverBinary.h"
#else
#include "Platform/io/IO_Main.h"
#include "Core/sync/Spinlock.h"
#endif

#ifdef IMPLUS_DRIVER_MODULE
static const driver_binary_t *g_driver_api = NULL;

#define outl g_driver_api->outl
#define inl g_driver_api->inl

#define hal_cpu_pause               g_driver_api->hal.cpu_pause
#define hal_cpu_save_interrupts     g_driver_api->hal.cpu_save_interrupts
#define hal_cpu_restore_interrupts  g_driver_api->hal.cpu_restore_interrupts

typedef struct { volatile int locked; } spinlock_t;
static inline void spinlock_init(spinlock_t *l)   { l->locked = 0; }
static inline void spinlock_lock(spinlock_t *l)   {
    while (__sync_lock_test_and_set(&l->locked, 1)) {
        while (l->locked) { hal_cpu_pause(); }
    }
}
static inline void spinlock_unlock(spinlock_t *l) { __sync_lock_release(&l->locked); }

static inline uint64_t irq_save_disable(void) { return hal_cpu_save_interrupts(); }
static inline void irq_restore(uint64_t flags) { hal_cpu_restore_interrupts(flags); }
#endif

static spinlock_t g_pci_lock = {0};

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset)
{
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)offset & 0xFCu);

    uint64_t flags = irq_save_disable();
    spinlock_lock(&g_pci_lock);
    outl(PCI_CONFIG_ADDRESS, address);
    uint32_t val = inl(PCI_CONFIG_DATA);
    spinlock_unlock(&g_pci_lock);
    irq_restore(flags);
    return val;
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) |
                       ((uint32_t)func << 8) |
                       ((uint32_t)offset & 0xFCu);

    uint64_t flags = irq_save_disable();
    spinlock_lock(&g_pci_lock);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
    spinlock_unlock(&g_pci_lock);
    irq_restore(flags);
}

uint32_t pci_read_bar(uint8_t bus, uint8_t device, uint8_t func, uint8_t bar_index)
{
    if (bar_index >= 6u) {
        return 0;
    }
    return pci_read_config(bus, device, func, (uint8_t)(0x10u + (bar_index * 4u)));
}

static void pci_read_bars(pci_device_t *dev)
{
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_bar(dev->bus, dev->device, dev->func, (uint8_t)i);
    }
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *out_device)
{
    if (out_device == NULL) {
        return 0;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config((uint8_t)bus, device, func, 0x00);
                uint16_t cur_vendor = (uint16_t)(vendor_device & 0xFFFFu);
                uint16_t cur_device = (uint16_t)((vendor_device >> 16) & 0xFFFFu);

                if (cur_vendor == 0xFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                if (cur_vendor == vendor_id && cur_device == device_id) {
                    uint32_t class_reg = pci_read_config((uint8_t)bus, device, func, 0x08);
                    out_device->bus = (uint8_t)bus;
                    out_device->device = device;
                    out_device->func = func;
                    out_device->vendor_id = cur_vendor;
                    out_device->device_id = cur_device;
                    out_device->class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
                    out_device->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
                    out_device->prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
                    pci_read_bars(out_device);
                    return 1;
                }

                if (func == 0u) {
                    uint32_t header_type = pci_read_config((uint8_t)bus, device, func, 0x0C);
                    if (((header_type >> 16) & 0x80u) == 0u) {
                        break;
                    }
                }
            }
        }
    }

    return 0;
}

void pci_scan_bus(void)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config((uint8_t)bus, device, func, 0x00);
                uint16_t vendor_id = (uint16_t)(vendor_device & 0xFFFFu);
                uint16_t device_id = (uint16_t)((vendor_device >> 16) & 0xFFFFu);

                if (vendor_id == 0xFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                uint32_t class_reg = pci_read_config((uint8_t)bus, device, func, 0x08);
                uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
                uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
                uint8_t prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);

                pci_device_t dev;
                dev.bus = (uint8_t)bus;
                dev.device = device;
                dev.func = func;
                dev.vendor_id = vendor_id;
                dev.device_id = device_id;
                dev.class_code = class_code;
                dev.subclass = subclass;
                dev.prog_if = prog_if;
                pci_read_bars(&dev);

                if (func == 0u) {
                    uint32_t header_type = pci_read_config((uint8_t)bus, device, func, 0x0C);
                    if (((header_type >> 16) & 0x80u) == 0u) {
                        break;
                    }
                }
            }
        }
    }
}

#ifdef IMPLUS_DRIVER_MODULE
static const pci_driver_t g_pci_driver = {
    .read_config = pci_read_config,
    .write_config = pci_write_config,
    .scan_bus = pci_scan_bus,
    .find_device = pci_find_device,
    .read_bar = pci_read_bar,
};

static void pci_driver_shutdown(void)
{
    g_driver_api = NULL;
}

static const driver_module_descriptor_t g_pci_module = {
    .magic = DRIVER_DESCRIPTOR_MAGIC,
    .version = DRIVER_DESCRIPTOR_VERSION,
    .kind = DEVICE_TYPE_PCI,
    .load_priority = 10u,
    .deps = { NULL },
    .driver_api = &g_pci_driver,
    .shutdown = pci_driver_shutdown,
};

#undef outl
#undef inl

const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api)
{
    if (api == NULL ||
        api->outl == NULL ||
        api->inl == NULL ||
        api->hal.cpu_pause == NULL ||
        api->hal.cpu_save_interrupts == NULL ||
        api->hal.cpu_restore_interrupts == NULL) {
        return NULL;
    }

    g_driver_api = api;
    return &g_pci_module;
}
#endif
