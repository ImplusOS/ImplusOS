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

#if defined(__aarch64__)
#define ARM64_QEMU_VIRT_PCI_ECAM_BASE 0x4010000000ULL
#define ARM64_PCI_ECAM_BUS_SHIFT      20u
#define ARM64_PCI_ECAM_DEVICE_SHIFT   15u
#define ARM64_PCI_ECAM_FUNC_SHIFT     12u
#define ARM64_PCI_ECAM_BUS_SIZE       (1ULL << ARM64_PCI_ECAM_BUS_SHIFT)
#define ARM64_PCI_ECAM_BUS_COUNT      256u

static volatile uint8_t *g_arm64_pci_ecam = NULL;
static uint8_t g_arm64_pci_bus_mapped[ARM64_PCI_ECAM_BUS_COUNT];

static bool arm64_pci_ensure_bus_mapped(uint8_t bus)
{
    if (g_arm64_pci_bus_mapped[bus] != 0u) {
        return true;
    }
    if (g_driver_api == NULL || g_driver_api->map_mmio_virt == NULL) {
        return false;
    }

    uint64_t bus_base = ARM64_QEMU_VIRT_PCI_ECAM_BASE +
                        ((uint64_t)bus * ARM64_PCI_ECAM_BUS_SIZE);
    if (g_driver_api->map_mmio_virt(bus_base) == NULL) {
        return false;
    }

    g_arm64_pci_bus_mapped[bus] = 1u;
    if (g_arm64_pci_ecam == NULL) {
        g_arm64_pci_ecam = (volatile uint8_t *)(uintptr_t)ARM64_QEMU_VIRT_PCI_ECAM_BASE;
    }
    return true;
}

static volatile uint32_t *arm64_pci_config_addr(uint8_t bus,
                                                uint8_t device,
                                                uint8_t func,
                                                uint8_t offset)
{
    if (!arm64_pci_ensure_bus_mapped(bus) || g_arm64_pci_ecam == NULL) {
        return NULL;
    }

    uint64_t ecam_offset =
        ((uint64_t)bus << ARM64_PCI_ECAM_BUS_SHIFT) |
        ((uint64_t)device << ARM64_PCI_ECAM_DEVICE_SHIFT) |
        ((uint64_t)func << ARM64_PCI_ECAM_FUNC_SHIFT) |
        ((uint64_t)offset & 0xFCu);

    return (volatile uint32_t *)(g_arm64_pci_ecam + ecam_offset);
}
#else
static spinlock_t g_pci_lock = {0};
#endif

uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset)
{
#if defined(__aarch64__)
    volatile uint32_t *addr = arm64_pci_config_addr(bus, device, func, offset);
    if (addr == NULL) {
        return 0xFFFFFFFFu;
    }
    return *addr;
#else
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
#endif
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value)
{
#if defined(__aarch64__)
    volatile uint32_t *addr = arm64_pci_config_addr(bus, device, func, offset);
    if (addr == NULL) {
        return;
    }
    *addr = value;
#else
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
#endif
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
#if defined(__aarch64__)
    g_arm64_pci_ecam = NULL;
    for (uint32_t i = 0; i < ARM64_PCI_ECAM_BUS_COUNT; ++i) {
        g_arm64_pci_bus_mapped[i] = 0u;
    }
#endif
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
