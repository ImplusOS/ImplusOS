#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "kernel/interfaces/device.h"

#define DRIVER_API_VERSION_MAJOR 2u
#define DRIVER_API_VERSION_MINOR 0u

#define DRIVER_DESCRIPTOR_MAGIC 0x44525641u
#define DRIVER_DESCRIPTOR_VERSION 2u
#define DRIVER_MAX_DEPS 4u

typedef struct __attribute__((packed)) {
    uint16_t keycode;
    uint8_t pressed;
    uint8_t ascii;
    uint8_t modifiers;
    uint8_t reserved[3];
} driver_keyboard_event_t;

typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
    int8_t wheel;
    uint8_t reserved[2];
} driver_mouse_event_t;

typedef struct {
    void (*msleep)(uint32_t ms);
    uint32_t (*hz)(void);
    uint64_t (*ticks)(void);
    uint64_t (*monotonic_ns)(void);
} driver_api_timer_t;

typedef struct {
    void *(*malloc)(uint64_t size);
    void (*free)(void *ptr);
    void *(*dma_alloc)(size_t size, uint64_t *phys_out);
    void (*dma_free)(void *ptr, size_t size);
    uint64_t (*virt_to_phys)(void *virt);
    void *(*memset)(void *s, int c, size_t n);
    void *(*memcpy)(void *dst, const void *src, size_t n);
    void *(*dma_alloc_ex)(size_t size,
                          size_t alignment,
                          uint64_t max_address,
                          uint64_t *phys_out);
} driver_api_mem_t;

typedef struct {
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t value);
} driver_api_io_t;

typedef struct {
    bool (*disk_read)(uint64_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*disk_write)(uint64_t lba, const uint8_t *buffer, uint32_t sector_count);
    uint64_t (*disk_get_partition_lba)(void);
    uint32_t (*pci_read_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
    void (*pci_write_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
    void *(*map_mmio_virt)(uint64_t phys_addr);
    void *(*map_mmio_range)(uint64_t phys_addr, size_t size);
} driver_api_hw_t;

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
} driver_pci_device_t;

typedef struct {
    uint64_t address;
    uint64_t size;
    uint8_t is_io;
    uint8_t is_64bit;
    uint8_t prefetchable;
    uint8_t reserved;
} driver_pci_bar_t;

typedef void (*driver_irq_handler_t)(void *context);

typedef struct {
    uint32_t (*get_device_count)(void);
    bool (*get_device)(uint32_t index, driver_pci_device_t *out_device);
    uint32_t (*read_config)(uint8_t bus,
                            uint8_t device,
                            uint8_t function,
                            uint16_t offset);
    void (*write_config)(uint8_t bus,
                         uint8_t device,
                         uint8_t function,
                         uint16_t offset,
                         uint32_t value);
    bool (*get_bar)(uint8_t bus,
                    uint8_t device,
                    uint8_t function,
                    uint8_t bar_index,
                    driver_pci_bar_t *out_bar);
    int32_t (*find_capability)(uint8_t bus,
                               uint8_t device,
                               uint8_t function,
                               uint8_t capability_id);
    bool (*enable_bus_master)(uint8_t bus,
                              uint8_t device,
                              uint8_t function);
    int32_t (*enable_msix)(uint8_t bus,
                           uint8_t device,
                           uint8_t function,
                           uint32_t requested_vectors,
                           uint32_t *out_vectors);
    void (*disable_msix)(uint8_t bus,
                         uint8_t device,
                         uint8_t function,
                         const uint32_t *vectors,
                         uint32_t vector_count);
    int32_t (*register_irq)(uint32_t vector,
                            driver_irq_handler_t handler,
                            void *context);
    void (*unregister_irq)(uint32_t vector);
} driver_api_pci_t;

typedef struct {
    void *(*create)(void);
    void (*destroy)(void *event);
    void (*reset)(void *event);
    void (*signal)(void *event);
    bool (*wait)(void *event, uint32_t timeout_ms);
} driver_api_event_t;

typedef struct {
    uint32_t (*cpu_count)(void);
    uint32_t (*current_cpu)(void);
} driver_api_system_t;

typedef struct {
    void (*write_char)(char c);
    void (*write_string)(const char *str);
    void (*write_uint32)(uint32_t val);
} driver_api_debug_t;

typedef struct {
    void (*cpu_halt)(void);
    void (*cpu_pause)(void);
    void (*cpu_enable_interrupts)(void);
    void (*cpu_disable_interrupts)(void);
    uint64_t (*cpu_save_interrupts)(void);
    void (*cpu_restore_interrupts)(uint64_t state);
    void (*mmu_invalidate_tlb)(uintptr_t addr);
    uint64_t (*cpu_read_cr)(int reg);
    void (*cpu_write_cr)(int reg, uint64_t value);
    void (*cpu_memory_barrier)(void);
    void (*io_delay)(void);
    uint64_t (*cpu_read_msr)(uint32_t msr);
    void (*cpu_write_msr)(uint32_t msr, uint64_t value);
    void (*cpu_get_id)(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx);
    void (*cpu_get_gdt_ptr)(void *ptr);
    void (*cpu_invalidate_caches)(void);
    void (*arch_switch_stack)(uintptr_t sp);
    uint64_t (*cpu_get_current_el)(void);
    void (*cpu_set_vbar)(void *vbar);
    uint64_t (*cpu_read_fs_base)(void);
    void (*cpu_write_fs_base)(uint64_t val);
    void (*cpu_save_fpu)(uint8_t *state);
    void (*cpu_restore_fpu)(uint8_t *state);

    void (*io_out8)(uint16_t port, uint8_t value);
    uint8_t (*io_in8)(uint16_t port);
    void (*io_out16)(uint16_t port, uint16_t value);
    uint16_t (*io_in16)(uint16_t port);
    void (*io_out32)(uint16_t port, uint32_t value);
    uint32_t (*io_in32)(uint16_t port);
    void (*io_outsw)(uint16_t port, const void *addr, uint32_t count);
} driver_api_hal_t;

typedef struct {
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t reserved;

    driver_api_timer_t timer;
    driver_api_mem_t mem;
    driver_api_io_t io;
    driver_api_hw_t hw;
    driver_api_debug_t dbg;
    driver_api_hal_t hal;
    driver_api_pci_t pci;
    driver_api_event_t event;
    driver_api_system_t system;

    void (*timer_msleep)(uint32_t ms);
    uint32_t (*timer_hz)(void);
    uint64_t (*timer_ticks)(void);
    void *(*malloc)(uint64_t size);
    void (*free)(void *ptr);
    void *(*dma_alloc)(size_t size, uint64_t *phys_out);
    void (*dma_free)(void *ptr, size_t size);
    uint64_t (*virt_to_phys)(void *virt);
    void *(*memset)(void *s, int c, size_t n);
    void *(*memcpy)(void *dst, const void *src, size_t n);
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t value);
    bool (*disk_read)(uint64_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*disk_write)(uint64_t lba, const uint8_t *buffer, uint32_t sector_count);
    uint64_t (*disk_get_partition_lba)(void);
    uint32_t (*pci_read_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
    void (*pci_write_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
    void *(*map_mmio_virt)(uint64_t phys_addr);
    void (*serial_write_char)(char c);
    void (*serial_write_string)(const char *str);
    void (*serial_write_uint32)(uint32_t val);
} driver_binary_t;

typedef struct {
    void (*init)(void);
    void (*poll)(void);
    int32_t (*read_keyboard)(driver_keyboard_event_t *out_event);
    int32_t (*read_mouse)(driver_mouse_event_t *out_event);
    void (*drain_keyboard)(driver_keyboard_event_t *tmp,
                           void (*forward)(driver_keyboard_event_t *));
    void (*drain_mouse)(driver_mouse_event_t *tmp,
                        void (*forward)(driver_mouse_event_t *));
} driver_input_t;

#define DRIVER_BLOCK_FLAG_WRITABLE  (1u << 0)
#define DRIVER_BLOCK_FLAG_REMOVABLE (1u << 1)
#define DRIVER_BLOCK_FLAG_BOOT      (1u << 2)
#define DRIVER_BLOCK_IDENTITY_PCI_VALID (1u << 0)

typedef enum {
    DRIVER_BLOCK_TRANSPORT_UNKNOWN = 0,
    DRIVER_BLOCK_TRANSPORT_ATA,
    DRIVER_BLOCK_TRANSPORT_USB,
    DRIVER_BLOCK_TRANSPORT_AHCI,
    DRIVER_BLOCK_TRANSPORT_NVME,
    DRIVER_BLOCK_TRANSPORT_VIRTIO,
} driver_block_transport_t;

typedef struct {
    uint64_t block_count;
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint32_t flags;
    driver_block_transport_t transport;
    uint32_t identity_flags;
    uint16_t pci_segment;
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint8_t reserved0;
    uint16_t controller_port;
    uint32_t namespace_id;
    char model[64];
    char serial[32];
} driver_block_info_t;

typedef struct {
    const char *name;
    uint32_t priority;
    bool (*init)(void);
    bool (*is_ready)(void);
    uint32_t (*get_device_count)(void);
    bool (*get_info)(uint32_t device_index, driver_block_info_t *out_info);
    bool (*read_blocks)(uint32_t device_index,
                        uint64_t lba,
                        void *buffer,
                        uint32_t block_count);
    bool (*write_blocks)(uint32_t device_index,
                         uint64_t lba,
                         const void *buffer,
                         uint32_t block_count);
    bool (*flush)(uint32_t device_index);
} driver_storage_t;

#define DRIVER_AUDIO_FORMAT_S16_LE 1u

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t format;
    uint32_t period_bytes;
    uint32_t period_count;
} driver_audio_info_t;

typedef struct {
    const char *name;
    uint32_t priority;
    bool (*init)(void);
    bool (*is_ready)(void);
    bool (*get_info)(driver_audio_info_t *out_info);
    bool (*open)(void);
    int64_t (*write)(const void *pcm, uint64_t bytes);
    bool (*drain)(uint32_t timeout_ms);
    void (*close)(void);
} driver_audio_t;

typedef struct {
    bool (*submit_interrupt_in_async)(uint8_t addr, uint8_t ep_num,
                                      uint16_t max_packet_size,
                                      void *dma_buf, uint64_t dma_phys,
                                      uint16_t length);
    int  (*check_interrupt_event)(uint8_t addr, uint8_t ep_num);
    bool (*submit_interrupt_in_sync)(uint8_t addr, uint8_t endpoint,
                                      uint16_t max_packet_size,
                                      void *data, uint16_t length);
} driver_usb_t;

typedef struct {
    driver_input_t   input;
    driver_storage_t storage;
    driver_usb_t     usb;
} usb_master_vtable_t;

typedef struct {
    void *addr;
    uint32_t size_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scan_line;
    uint32_t bytes_per_pixel;
} driver_boot_framebuffer_t;

typedef struct {
    const char *name;
    bool (*probe)(void);
    bool (*init)(void);
    bool (*is_ready)(void);
    uint32_t (*width)(void);
    uint32_t (*height)(void);
    void (*draw_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*present)(void);
    bool (*set_framebuffer)(const driver_boot_framebuffer_t *framebuffer);
    void *(*get_framebuffer)(void);
} driver_display_t;

#define DRIVER_KBD_MOD_SHIFT (1u << 0)
#define DRIVER_KBD_MOD_CTRL  (1u << 1)
#define DRIVER_KBD_MOD_ALT   (1u << 2)
#define DRIVER_KBD_MOD_CAPS  (1u << 3)

typedef void (*driver_nic_rx_callback_t)(const uint8_t *frame, uint16_t frame_len);

typedef struct {
    bool (*init)(void);
    bool (*is_ready)(void);
    uint16_t (*mtu)(void);
    void (*get_mac)(uint8_t mac_out[6]);
    bool (*send_frame)(const uint8_t *frame, uint16_t frame_len);
    void (*poll)(void);
    void (*set_rx_callback)(driver_nic_rx_callback_t cb);
} driver_nic_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    device_type_t kind;
    uint32_t load_priority;
    const char *deps[DRIVER_MAX_DEPS];
    const void *driver_api;
    void (*shutdown)(void);
} driver_module_descriptor_t;

typedef const driver_module_descriptor_t *(*driver_module_init_fn_t)(const driver_binary_t *api);
