# Driver Module Development Guide

This document describes how to create a new driver module for ImplusOS.

---

## 1. Overview

ImplusOS drivers are built as **position-independent ELF shared objects**
(`ET_DYN`) that are pre-loaded by the bootloader and lazily initialised by
the kernel. Each driver module exports an initialisation function that receives
a kernel API table and returns a vtable of driver operations.

---

## 2. Adding a New Driver Module

### Step 1: Assign a Module ID

Edit `Kernel/Drivers/DriverModuleIds.h`:

```c
enum {
    DRIVER_MODULE_ID_INVALID = 0,
    DRIVER_MODULE_ID_PCI = 1,
    DRIVER_MODULE_ID_FAT32 = 2,
    DRIVER_MODULE_ID_PS2 = 3,
    DRIVER_MODULE_ID_DISPLAY_VIRTIO = 4,
    DRIVER_MODULE_ID_DISPLAY_IMPLUS_DISPLAY_GENERIC_DRIVER = 5,
    DRIVER_MODULE_ID_USB = 6,
    DRIVER_MODULE_ID_MY_DRIVER = 7,      // ← Add your ID
    DRIVER_MODULE_ID_MAX = 7             // ← Update MAX
};
```

### Step 2: Create the Driver Source

Create your source file, e.g., `Kernel/Drivers/MyDriver/MyDriver.c`:

```c
#include "../DriverBinary.h"

static const driver_kernel_api_t *g_api;

// Your driver's operation implementations
static bool my_driver_probe(void) {
    // Return true if hardware is present
    return true;
}

static bool my_driver_init_hw(void) {
    // Initialise hardware
    g_api->serial_write_string("[MyDriver] Initialised\n");
    return true;
}

// The vtable your driver exports — define per your driver type

// Entry point — MUST be named driver_module_init
const void *driver_module_init(const driver_kernel_api_t *api) {
    g_api = api;

    if (!my_driver_probe()) {
        return NULL;
    }

    my_driver_init_hw();

    // Return your driver vtable (cast as appropriate)
    return &my_driver_vtable;
}
```

### Step 3: Add Build Rules to Makefile

```makefile
# Add object file
$(BUILD_DIR)/Modules/MyDriver_Module.o: Kernel/Drivers/MyDriver/MyDriver.c
	mkdir -p $(dir $@)
	$(CC) $(DRIVER_MODULE_CFLAGS) -c $< -o $@

# Add ELF target
MY_DRIVER_ELF := $(BUILD_DIR)/Kernel/Drivers/MyDriver.ELF

$(MY_DRIVER_ELF): $(BUILD_DIR)/Modules/MyDriver_Module.o
	mkdir -p $(dir $@)
	$(LD) $(DRIVER_MODULE_LDFLAGS) $^ -o $@

# Add to 'all' target
all: ... $(MY_DRIVER_ELF)
```

### Step 4: Register in Bootloader

Edit `BootLoader/Loader.c`, in `PreloadDriverModules()`:

```c
static PRELOAD_FILE_SPEC Specs[] = {
    // ... existing entries ...
    { DRIVER_MODULE_ID_MY_DRIVER, L"Kernel\\Driver\\MyDriver.ELF", TRUE },
};
```

### Step 5: Load from Kernel Side

Create a client file (e.g., `Kernel/Drivers/MyDriver/MyDriver_Client.c`):

```c
#include "../DriverModule.h"

static const my_driver_vtable_t *g_driver = NULL;

void my_driver_client_init(void) {
    uint64_t entry = 0;
    if (!driver_module_manager_load(DRIVER_MODULE_ID_MY_DRIVER,
                                     1024 * 1024, 4 * 1024 * 1024,
                                     &entry)) {
        return;
    }

    typedef const my_driver_vtable_t *(*init_fn)(const driver_kernel_api_t *);
    init_fn fn = (init_fn)entry;
    g_driver = fn(driver_module_manager_kernel_api());
}
```

---

## 3. Build Flags

Driver modules **must** be compiled with:

| Flag | Purpose |
|---|---|
| `-fPIC` | Position-independent code (mandatory for `ET_DYN`) |
| `-DIMPLUS_DRIVER_MODULE` | Signals driver context |
| `-DKERNEL` | Access kernel headers |

And linked with:

| Flag | Purpose |
|---|---|
| `-shared` | Produce `ET_DYN` ELF |
| `-Bsymbolic` | Bind references to local definitions |
| `-e driver_module_init` | Set entry point |
| `-z max-page-size=4096` | 4 KiB page alignment |

---

## 4. Kernel API Available to Drivers

The `driver_kernel_api_t` provides:

### Timer
- `timer_msleep(uint32_t ms)` — busy-wait sleep
- `timer_hz()` — current timer frequency
- `timer_ticks()` — current tick count

### Memory
- `malloc(uint64_t size)` / `free(void *ptr)` — heap allocation
- `dma_alloc(size_t size, uint64_t *phys_out)` — DMA buffer (physical address returned)
- `dma_free(void *ptr, size_t size)` — free DMA buffer
- `virt_to_phys(void *virt)` — virtual → physical translation

### Memory Operations
- `memset(void *s, int c, size_t n)`
- `memcpy(void *dst, const void *src, size_t n)`

### Port I/O
- `inb(uint16_t port)` / `outb(uint16_t port, uint8_t value)`
- `inl(uint16_t port)` / `outl(uint16_t port, uint32_t value)`

### Disk
- `disk_read(uint32_t lba, uint8_t *buffer, uint32_t sector_count)`
- `disk_write(uint32_t lba, const uint8_t *buffer, uint32_t sector_count)`

### PCI
- `pci_read_config(bus, device, func, offset)`
- `pci_write_config(bus, device, func, offset, value)`

### MMIO
- `map_mmio_virt(uint64_t phys_addr)` — map physical MMIO to virtual

### Debug Output
- `serial_write_char(char c)`
- `serial_write_string(const char *str)`
- `serial_write_uint32(uint32_t val)`

---

## 5. Restrictions

- No standard library (no `libc`, no `printf`)
- No direct access to kernel globals — use the API table
- No dynamic linking to other modules
- Module code runs in kernel space (Ring 0) — bugs can crash the system
- Maximum 4096 relocations per module
