# Boot Sequence — Detailed Reference

This document provides a step-by-step reference for the ImplusOS boot sequence
from UEFI firmware entry to userland transition.

---

## 1. UEFI Bootloader (`BootLoader/Loader.c`)

Entry: `efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST)`

### 1.1 Initialisation

```
1. InitializeLib(ImageHandle, ST)
2. Get EFI_LOADED_IMAGE_PROTOCOL for ImageHandle
3. Get EFI_SIMPLE_FILE_SYSTEM_PROTOCOL from boot device
4. Open root volume → EFI_FILE_PROTOCOL *Root
5. Locate EFI_GRAPHICS_OUTPUT_PROTOCOL (GOP)
```

### 1.2 Display Setup

```
6. FillScreen(Gop, 0x000000)    — clear to black
7. DisplayBMP(ST, Root, Gop)    — render EFI/BOOT/Resource/Images/BootLogo.bmp with alpha blending
8. DrawTextGraySmallCenterBottom() - render system info text using NotoSans font
```

The BMP renderer supports:
- 24-bit and 32-bit BMP formats
- Top-down and bottom-up row order
- All GOP pixel formats (RGB, BGR, BitMask)
- Alpha blending for 32-bit BMPs

### 1.3 Boot Info Collection

```
8.  Store GOP framebuffer info into BOOT_INFO
9.  GetPartitionStartLBA() — detect boot partition
      ├─ First: check EFI Device Path for HARDDRIVE_DEVICE_PATH
      ├─ Fallback: scan block devices for El Torito catalog
      └─ Default: 2048 (standard GPT offset)
10. Detect boot drive type from Device Path:
      ├─ Messaging/USB (subtype 5) → BOOT_DRIVE_TYPE_USB
      └─ Messaging/ATA (subtype 1 or 18) → BOOT_DRIVE_TYPE_IDE
11. DiscoverAcpiRsdp() — search EFI Configuration Tables
      ├─ Prefer ACPI 2.0 table (XSDT support)
      └─ Fall back to ACPI 1.0 table
```

### 1.4 Kernel Loading

```
12. Open Kernel\Kernel_Main.ELF from FAT32
13. LoadFileToMemoryBelow4G() — allocate pages < 4 GiB
14. LoadKernelELF() — parse ELF64:
      ├─ Verify ELF magic (0x7F 'E' 'L' 'F')
      ├─ For each PT_LOAD segment:
      │   ├─ AllocatePages at physical address
      │   ├─ memcpy file data
      │   └─ memset BSS to zero
      └─ Return entry point from ELF header
15. Free the file buffer (pages are kept)
```

### 1.5 Driver Module Preloading

```
16. PreloadDriverModules() — for each driver in /Kernel/Driver/:
      ├─ Open ELF file
      ├─ LoadFileToMemoryBelow4G() — keep in low memory
      ├─ Store in BOOT_INFO.LoadedFiles[]:
      │   ├─ .Name      = filename (char8)
      │   ├─ .PhysAddr  = physical address of ELF blob
      │   └─ .Size      = file size
      └─ Skip on failure
```

The bootloader automatically preloads any `.ELF` files found in the `/Kernel/Driver/` directory. The kernel's `driver_module_manager_init` will then process these loaded images based on their embedded metadata.

### 1.6 Exit Boot Services

```
17. ExitBootServicesComplete() — retry loop:
      ├─ GetMemoryMap (with 8 extra descriptors margin)
      ├─ ExitBootServices(MapKey)
      ├─ Retry if MapKey is stale
      └─ Store final memory map in BOOT_INFO
```

### 1.7 Kernel Handoff

```
18. Cast entry point to KernelEntryFn
19. Entry(&BootInfo) — never returns
```

---

## 2. Kernel Initialisation (`Kernel/Kernel_Main.c`)

Entry: `kernel_main(BOOT_INFO *boot_info)`

### Phase 1: CPU Foundation (Interrupts Disabled)

| Step | Function | Description |
|---|---|---|
| 1 | `cli` | Disable interrupts |
| 2 | `memcpy` | Copy BOOT_INFO to static `g_boot_info_copy` |
| 3 | Stack switch | Move RSP to `_stack_top` (256 KiB kernel stack) |
| 4 | `load_bar_init` | Setup progress bar animation |
| 5 | `timer_set_callback` | Register spinner animation callback |
| 6 | `serial_init` | COM1 at 115200 baud |
| 7 | `init_gdt` | 6 segments + TSS |
| 8 | `init_idt` | 256 IDT entries |
| 9 | `init_physical_memory` | Bitmap PMM from EFI memory map |
| 10 | `init_paging` | 4-level page tables |
| 11 | `memory_init` | Kernel heap allocator |

### Phase 2: Platform (SMP + Interrupts)

| Step | Function | Description |
|---|---|---|
| 12 | `smp_init` | Detect and wake APs |
| 13 | `acpi_init` | Parse RSDP → MADT |
| 14 | `platform_interrupts_configure_from_acpi` | LAPIC + I/O APIC setup |
| 15 | `timer_init(60)` | 60 Hz PIT timer |
| 16 | `sti` | Enable interrupts |
| 17 | `timer_try_switch_to_lapic` | Upgrade to LAPIC timer if available |

### Phase 3: Drivers & Services

| Step | Function | Description |
|---|---|---|
| 18 | `driver_module_manager_init` | Register pre-loaded driver ELFs |
| 19 | `driver_select_set_boot_framebuffer` | Hand off GOP framebuffer |
| 20 | `disk_io_init` | Select ATA or USB Mass Storage protocol |
| 21 | `all_fs_initialize` | FAT32 + VFS init |
| 22 | `display_init` | Probe and init display driver |
| 23 | `debugger_init` | Debug subsystem |
| 24 | `wm_kernel_init` | Window Manager kernel side |
| 25 | `ps2_input_init` | PS/2 keyboard/mouse via driver module |
| 26 | `usb_driver_client_init` | USB HCI via driver module |
| 27 | `network_stack_init` | Ethernet → ARP → IPv4 → UDP |
| 28 | `syscall_init` | MSR setup for SYSCALL/SYSRET |
| 29 | `process_manager_init` | Process table init |
| 30 | `ipc_init` | Message queues |
| 31 | `syscall_file_init` | File descriptor table |

### Phase 4: Userland Entry

```
32. load_bar_finish()     — complete progress animation
33. Clear screen to black
34. Register /Userland/Userland.ELF as PID 0
35. entry_user_mode():
      ├─ Set CR3 to user page table
      ├─ Set DS/ES/FS/GS to user data selector (0x20 | 3)
      ├─ Push SS, RSP, RFLAGS(0x202), CS, RIP
      └─ iretq → Ring 3 at _start()
```

---

## 3. Userland Init (`Userland/Userland.c`)

The init process spawns system services in order:

```
1. com_ImplusOS_windowmanager  → Window Manager service
2. com_ImplusOS_mousemanager   → Mouse cursor manager
3. com_ImplusOS_gui_demo       → GUI demo application
4. com_ImplusOS_exampleApp     → Example user application
5. com_ImplusOS_system         → System services
6. com_ImplusOS_NetworkTest    → Network test application
```

Each spawn uses fallback paths (e.g., `/Userland/SystemApps/...` first,
then `/Userland/...`). After spawning all processes, the init process enters
an infinite yield loop.
