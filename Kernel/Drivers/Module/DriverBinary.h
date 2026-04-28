#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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

    bool (*disk_read)(uint32_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*disk_write)(uint32_t lba, const uint8_t *buffer, uint32_t sector_count);

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

typedef struct {
    bool (*read_sectors)(uint32_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*write_sectors)(uint32_t lba, const uint8_t *buffer, uint32_t sector_count);
} driver_storage_t;

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
    const void *driver_api;
    void (*shutdown)(void);
} driver_module_descriptor_t;

typedef const driver_module_descriptor_t *(*driver_module_init_fn_t)(const driver_binary_t *api);
