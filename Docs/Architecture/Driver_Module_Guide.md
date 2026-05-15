# Driver Module Guide — ImplusOS

## 1. Overview

ImplusOS uses a **loadable driver module system**. Drivers are compiled as **PIC
(Position-Independent Code) ELF shared objects** that the kernel loads at boot
time. The kernel provides a vtable of kernel services (`driver_binary_t`), and
drivers export an initialization function that returns a descriptor with the
driver's API.

## 2. Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        Kernel Core                           │
│                                                              │
│  ┌──────────────────────────────────────────────────────────┐│
│  │               Driver Module Manager                      ││
│  │                                                          ││
│  │  1. Load ELF from boot filesystem                        ││
│  │  2. Parse ELF headers, apply relocations                 ││
│  │  3. Find driver_module_init symbol                       ││
│  │  4. Call driver_module_init(kernel_api)                   ││
│  │  5. Receive driver_module_descriptor_t                    ││
│  │  6. Attach driver to Driver Manager                      ││
│  └──────────────────────────────────────────────────────────┘│
│                                                              │
│  ┌──────────────────────────────────────────────────────────┐│
│  │               Driver Manager                             ││
│  │                                                          ││
│  │  Categorizes drivers by kind:                            ││
│  │  - PCI, FAT32, Display, Input, USB, NIC                  ││
│  │  Provides unified access: driver_manager_get_*()         ││
│  └──────────────────────────────────────────────────────────┘│
│                                                              │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐│
│  │ Client APIs │ │ Server Impl │ │   Module Framework      ││
│  │ (kernel-    │ │ (actual     │ │   (load/unload/         ││
│  │  facing)    │ │  drivers)   │ │    init/reload)          ││
│  └─────────────┘ └─────────────┘ └─────────────────────────┘│
└──────────────────────────────────────────────────────────────┘
```

## 3. Driver Categories

| Category | Kind Enum | Example Drivers | Key Interface |
|---|---|---|---|
| PCI Bus | `DRIVER_MANAGER_KIND_PCI` | `PCI_Driver.ELF` | `pci_driver_t` |
| Filesystem | `DRIVER_MANAGER_KIND_FAT32` | `FAT32_Driver.ELF` | `fat32_driver_t` |
| Display | `DRIVER_MANAGER_KIND_DISPLAY` | `ImplusOS_Generic_Display_Driver.ELF`, `VirtIO_Driver.ELF` | `driver_display_t` |
| Input | `DRIVER_MANAGER_KIND_INPUT` | `PS2_Driver.ELF` | `driver_input_t` |
| USB Host | `DRIVER_MANAGER_KIND_USB` | `USB_Driver.ELF` | `usb_master_vtable_t` |
| NIC | `DRIVER_MANAGER_KIND_NIC` | `VirtIO_Driver.ELF` | `driver_nic_t` |

## 4. Kernel API (`driver_binary_t`)

The kernel passes this vtable to every driver during initialization:

```c
typedef struct {
    // Timer
    void (*timer_msleep)(uint32_t ms);
    uint32_t (*timer_hz)(void);
    uint64_t (*timer_ticks)(void);

    // Memory allocation
    void *(*malloc)(uint64_t size);
    void (*free)(void *ptr);

    // DMA (physically contiguous memory)
    void *(*dma_alloc)(size_t size, uint64_t *phys_out);
    void (*dma_free)(void *ptr, size_t size);
    uint64_t (*virt_to_phys)(void *virt);

    // Memory operations
    void *(*memset)(void *s, int c, size_t n);
    void *(*memcpy)(void *dst, const void *src, size_t n);

    // I/O ports
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t value);

    // Block I/O
    bool (*disk_read)(uint32_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*disk_write)(uint32_t lba, const uint8_t *buffer, uint32_t sector_count);

    // PCI configuration space
    uint32_t (*pci_read_config)(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
    void (*pci_write_config)(uint8_t bus, uint8_t device, uint8_t func,
                              uint8_t offset, uint32_t value);

    // MMIO mapping
    void *(*map_mmio_virt)(uint64_t phys_addr);

    // Debug output
    void (*serial_write_char)(char c);
    void (*serial_write_string)(const char *str);
    void (*serial_write_uint32)(uint32_t val);
} driver_binary_t;
```

## 5. Driver Descriptor

Every driver module must export:

```c
typedef struct {
    const void *driver_api;    // Pointer to the driver's API struct
    void (*shutdown)(void);    // Called when driver is unloaded
} driver_module_descriptor_t;

typedef const driver_module_descriptor_t *(*driver_module_init_fn_t)(
    const driver_binary_t *api
);
```

The kernel looks for the symbol `driver_module_init` in the loaded ELF.

## 6. Writing a New Driver

### 6.1 Source Structure

Create a directory under `Kernel/Drivers/Server/<Category>/<DriverName>/`:

```
Kernel/Drivers/Server/
└── MyCategory/
    └── MyDriver/
        ├── Makefile
        ├── MyDriver.c
        └── MyDriver.h
```

### 6.2 Implementation Template

```c
// MyDriver.c
#include "Drivers/Module/DriverBinary.h"

static const driver_binary_t *g_api = NULL;

// Your driver's API implementation
static bool my_driver_init(void) {
    g_api->serial_write_string("MyDriver: Initializing...\n");
    // Initialize hardware, allocate resources
    return true;
}

static void my_driver_shutdown(void) {
    g_api->serial_write_string("MyDriver: Shutting down\n");
    // Release resources
}

// Fill in your driver-specific vtable
static const driver_display_t my_display_ops = {
    .name       = "MyDriver",
    .probe      = my_driver_probe,
    .init       = my_driver_init,
    .is_ready   = my_driver_is_ready,
    .width      = my_driver_width,
    .height     = my_driver_height,
    .draw_pixel = my_driver_draw_pixel,
    .fill_rect  = my_driver_fill_rect,
    .present    = my_driver_present,
    // ...
};

static const driver_module_descriptor_t descriptor = {
    .driver_api = &my_display_ops,
    .shutdown   = my_driver_shutdown,
};

// Required export symbol
const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api) {
    g_api = api;
    return &descriptor;
}
```

### 6.3 Makefile Template

```makefile
include ../../../module.mk
```

The `module.mk` file provides standard build rules for driver modules.

### 6.4 Client Interface

If your driver needs a kernel-facing client API, add files under
`Kernel/Drivers/Client/<Category>/`:

```
Kernel/Drivers/Client/
└── MyCategory/
    ├── MyDriver_Client.c
    └── MyDriver_Main.h
```

### 6.5 Registration

The driver is automatically discovered when placed in `Kernel/Driver/` on the
boot filesystem. The `driver_module_manager_init()` function loads all ELFs
from the boot info's loaded files, and `driver_module_init_all()` calls each
driver's init function.

## 7. Driver Interface Specifications

### 7.1 Display Driver (`driver_display_t`)

```c
typedef struct {
    const char *name;
    bool (*probe)(void);                                    // Check if hardware is present
    bool (*init)(void);                                     // Initialize the display
    bool (*is_ready)(void);                                 // Is display ready for rendering?
    uint32_t (*width)(void);                                // Display width in pixels
    uint32_t (*height)(void);                               // Display height in pixels
    void (*draw_pixel)(uint32_t x, uint32_t y, uint32_t color);
    void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    void (*present)(void);                                  // Swap/flush buffers
    bool (*set_framebuffer)(const driver_boot_framebuffer_t *fb);
    void *(*get_framebuffer)(void);                         // Direct framebuffer access
} driver_display_t;
```

### 7.2 Input Driver (`driver_input_t`)

```c
typedef struct {
    void (*init)(void);
    void (*poll)(void);                                     // Poll for new events
    int32_t (*read_keyboard)(driver_keyboard_event_t *out); // 0 = no event, 1 = event
    int32_t (*read_mouse)(driver_mouse_event_t *out);       // 0 = no event, 1 = event
    void (*drain_keyboard)(driver_keyboard_event_t *tmp,
                           void (*forward)(driver_keyboard_event_t *));
    void (*drain_mouse)(driver_mouse_event_t *tmp,
                        void (*forward)(driver_mouse_event_t *));
} driver_input_t;
```

### 7.3 Keyboard Event

```c
typedef struct __attribute__((packed)) {
    uint16_t keycode;      // Scancode
    uint8_t  pressed;      // 1 = press, 0 = release
    uint8_t  ascii;        // ASCII character (0 if non-printable)
    uint8_t  modifiers;    // SHIFT | CTRL | ALT | CAPS
    uint8_t  reserved[3];
} driver_keyboard_event_t;
```

Modifier flags:
- `DRIVER_KBD_MOD_SHIFT` (bit 0)
- `DRIVER_KBD_MOD_CTRL` (bit 1)
- `DRIVER_KBD_MOD_ALT` (bit 2)
- `DRIVER_KBD_MOD_CAPS` (bit 3)

### 7.4 Mouse Event

```c
typedef struct __attribute__((packed)) {
    uint16_t x;            // Absolute X position
    uint16_t y;            // Absolute Y position
    uint8_t  buttons;      // Button state bitmask
    int8_t   wheel;        // Scroll wheel delta
    uint8_t  reserved[2];
} driver_mouse_event_t;
```

### 7.5 Storage Driver (`driver_storage_t`)

```c
typedef struct {
    bool (*read_sectors)(uint32_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*write_sectors)(uint32_t lba, const uint8_t *buffer, uint32_t sector_count);
} driver_storage_t;
```

### 7.6 NIC Driver (`driver_nic_t`)

```c
typedef struct {
    bool (*init)(void);
    bool (*is_ready)(void);
    uint16_t (*mtu)(void);
    void (*get_mac)(uint8_t mac_out[6]);
    bool (*send_frame)(const uint8_t *frame, uint16_t frame_len);
    void (*poll)(void);
    void (*set_rx_callback)(driver_nic_rx_callback_t cb);
} driver_nic_t;
```

### 7.7 USB Master (`usb_master_vtable_t`)

```c
typedef struct {
    driver_input_t   input;    // USB HID input
    driver_storage_t storage;  // USB Mass Storage
    driver_usb_t     usb;      // Low-level USB transfer API
} usb_master_vtable_t;
```

## 8. Existing Driver Modules

### PCI Driver (`PCI_Driver.ELF`)

- Source: `Kernel/Drivers/Server/PCI/PCI_Main.c`
- Enumerates PCI bus devices
- Provides config space read/write

### FAT32 Driver (`FAT32_Driver.ELF`)

- Source: `Kernel/Drivers/Server/FileSystem/FAT32/FAT32_Main.c`
- Full FAT32 filesystem implementation
- Read/write files, directories, create/delete
- BPB parsing, cluster chain walking, FAT table management

### PS/2 Driver (`PS2_Driver.ELF`)

- Source: `Kernel/Drivers/Server/PS2/PS2_Input.c`
- PS/2 keyboard and mouse input
- Scancode translation to ASCII
- Mouse packet parsing

### USB Driver (`USB_Driver.ELF`)

- Source: `Kernel/Drivers/Server/USB/`
- Host controller support: OHCI, UHCI, EHCI, XHCI
- Device classes: HID (keyboard/mouse), Mass Storage
- Hub enumeration and device setup

### VirtIO Driver (`VirtIO_Driver.ELF`)

- Source: `Kernel/Drivers/Server/Display/VirtIO/` and `Kernel/Drivers/Server/NIC/VirtIONet/`
- VirtIO-GPU display driver (double-buffered)
- VirtIO-Net NIC driver (Ethernet frame send/receive)

### Generic Display Driver (`ImplusOS_Generic_Display_Driver.ELF`)

- Source: `Kernel/Drivers/Server/Display/ImplusOS_Generic/ImplusOS_Generic.c`
- Fallback framebuffer driver using the boot framebuffer from UEFI GOP
- Double-buffered rendering

## 9. Hot Reload

Drivers can be unloaded and reloaded at runtime:

```c
// Unload a driver
driver_module_manager_unload_by_name("MyDriver");

// Reload a driver
driver_module_manager_reload_by_name("MyDriver");
```

This calls the driver's `shutdown()` callback, unlinks from the Driver Manager,
then re-loads and re-initializes the driver ELF.

## 10. Driver Selection

The `DriverSelect` module (`Kernel/Drivers/Module/DriverSelect.c`) handles:
- Setting the boot framebuffer from UEFI GOP
- Selecting the best available display driver (VirtIO-GPU preferred, fallback to generic)
