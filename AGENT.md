# AGENT.md — ImplusOS Project Guide for AI Coding Agents

> This file provides project context for AI coding assistants (OpenAI Codex, Copilot, Cursor, Windsurf, Devin, etc.).

## What Is This Project?

**ImplusOS** is a hobby operating system written in C and x86-64 assembly.

- **Target**: x86-64 (Long Mode), UEFI boot
- **Kernel**: Monolithic with loadable PIC ELF driver modules
- **Userland**: Custom init process, apps communicate via `SYSCALL`/`SYSRET`
- **POSIX**: Compatibility layer for porting C programs
- **License**: MIT

## Repository Map

| Directory | Purpose |
|---|---|
| `BootLoader/` | UEFI and BIOS bootloader entry points (`x86_64/UEFI/`, `x86_64/BIOS/`) |
| `BootManager/` | Secondary boot stage logic (UEFI and BIOS) |
| `Kernel/` | All kernel subsystems |
| `Kernel/Arch/x86_64/` | GDT, IDT, paging (mmu), SMP, VMX (virt) |
| `Kernel/Core/` | kernel_main and core subsystems (process, syscall, vfs, window, timer, sync, elf) |
| `Kernel/Debug/` | Serial (COM1), printf, panic handler |
| `Kernel/Drivers/` | Driver framework: Module (loader), Client (kernel-side API), Server (driver implementations) |
| `Kernel/MemoryManagement/` | Bitmap PMM (Memory_Main), DMA allocator |
| `Kernel/Network/` | Full network stack: Ethernet, ARP, IPv4, UDP, TCP, ICMP, DHCP |
| `Kernel/Platform/` | ACPI, IOAPIC, LAPIC, I/O (ATA, USB Mass Storage protocols) |
| `Kernel/include/` | Shared interfaces: `arch_ops.h`, `driver_api.h`, `status.h`, `config.h` |
| `Userland/` | User-space code |
| `Userland/API/` | Typed syscall wrapper headers |
| `Userland/Application/` | Apps: WindowManager, Shell, Editor, FileManager, Clock, VM, NetworkTest |
| `Userland/POSIX/` | POSIX compatibility (open/read/write/fork/socket/pthread/signal/time) |
| `libc/` | Minimal freestanding C library |
| `Thirdparty/` | stb_truetype.h, stb_image.h |

## Build Instructions

### Prerequisites (Ubuntu/Debian)

```bash
sudo apt install -y build-essential nasm binutils gnu-efi \
  gcc-x86-64-elf g++-x86-64-elf parted qemu-system-x86 \
  dosfstools xorriso mtools util-linux gdb
```

### Commands

```bash
make                # Build everything
make image_esp      # Create bootable ISO (Hybrid BIOS/UEFI)
make run_uefi_usb   # Run in QEMU (UEFI USB boot)
make run_uefi_cdrom # Run in QEMU (UEFI CD-ROM boot)
make run_bios_usb   # Run in QEMU (BIOS USB boot)
make clean          # Remove build artifacts
```

### Cross-Compiler

All code is compiled with `x86_64-elf-gcc` (freestanding, no stdlib).

## Architecture Quick Reference

### Boot Sequence

```
UEFI/BIOS → Loader → BootManager → Load kernel ELF + drivers → kernel_main()
↓
serial → GDT → IDT → PMM → paging → heap → ACPI → interrupts → syscall →
SMP → VMX → timer → drivers → FS → display → WM → processes → IPC → network →
launch Userland.ELF (init)
```

### Syscall ABI

- Instruction: AMD64 `SYSCALL`/`SYSRET`
- Number in `RAX`, args in `RDI, RSI, RDX, R10, R8`
- Return in `RAX`
- All numbers: `Kernel/Core/syscall/Syscall_Main.h`
- Dispatch: `Kernel/Core/syscall/Syscall_Dispatch.c`

### Error Codes

```c
typedef int64_t os_status_t;
// OS_STATUS_OK            =   0
// OS_STATUS_NOT_FOUND     =  -2
// OS_STATUS_IO_ERROR      =  -5
// OS_STATUS_ACCESS_DENIED = -13
// OS_STATUS_FAULT         = -14
// OS_STATUS_INVALID_ARG   = -22
// OS_STATUS_LIMIT_REACHED = -24
// OS_STATUS_NOT_SUPPORTED = -95
// OS_STATUS_INTERNAL      = -255
```

### Process Capabilities

```c
PROCESS_CAP_SERIAL   (1 << 0)   // Serial I/O
PROCESS_CAP_PROCESS  (1 << 1)   // Create/spawn processes
PROCESS_CAP_FILE     (1 << 2)   // File operations
PROCESS_CAP_MEMORY   (1 << 3)   // Memory allocation
PROCESS_CAP_INPUT    (1 << 4)   // Keyboard/mouse input
PROCESS_CAP_SIGNAL   (1 << 5)   // Signal handling
PROCESS_CAP_IPC      (1 << 6)   // IPC messaging
PROCESS_CAP_NETWORK  (1 << 7)   // Network access
```

### User-Space Memory Map

| Region | Address Range |
|---|---|
| Code | `0x0000004000000000` – `0x0000004080000000` |
| Heap | `0x0000004100000000` – `0x00000047E0000000` |
| Stack | `0x00000047E0000000` – `0x0000004800000000` (32 MiB) |

### Driver Module Interface

Drivers are PIC ELF shared objects. The kernel passes a `driver_binary_t` vtable:

```c
typedef struct {
    void (*timer_msleep)(uint32_t ms);
    void *(*malloc)(uint64_t size);
    void (*free)(void *ptr);
    void *(*dma_alloc)(size_t size, uint64_t *phys_out);
    void (*dma_free)(void *ptr, size_t size);
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    bool (*disk_read)(uint32_t lba, uint8_t *buffer, uint32_t sector_count);
    bool (*disk_write)(uint32_t lba, const uint8_t *buffer, uint32_t sector_count);
    uint32_t (*pci_read_config)(...);
    void *(*map_mmio_virt)(uint64_t phys_addr);
    void (*serial_write_string)(const char *str);
    // ... more
} driver_binary_t;
```

Drivers export: `const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api);`

### Kernel Configuration (Compile-Time)

See `Kernel/include/kernel/config.h`:
- `OS_CONFIG_PROCESS_MAX_COUNT` — Max processes (default: 128, range: 1–256)
- `OS_CONFIG_FILE_MAX_FD` — Max file descriptors per process (default: 32, range: 4–256)
- `OS_CONFIG_SMP_MAX_CPUS` — Max CPU cores (default: 4)
- `OS_CONFIG_NET_IPV4_ADDR` — Static IPv4 address (default: 10.0.2.15)

## Coding Conventions

1. **Language**: C11 + NASM assembly (x86-64)
2. **Naming**: `PascalCase` for types/structs, `snake_case` for functions, `UPPER_CASE` for macros
3. **Headers**: Use `#pragma once`
4. **No standard library**: Everything is freestanding. Use `libc/` for string/math/stdlib
5. **Warnings**: `-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow`
6. **Driver modules**: Must be position-independent (`-fPIC`)
7. **Comments**: Preserve existing comments when modifying code

## Testing

- QEMU + OVMF firmware
- Machine: q35, 4 CPUs (Skylake-Server+VMX), 4GB RAM
- Devices: VirtIO-Net, XHCI USB (kbd+mouse), NVMe, HDA audio
- Serial: COM1 via `-serial stdio` (115200 baud)
- Boot: USB stick or IDE CD-ROM

## Important Files for AI Agents

When working on this codebase, these files are most frequently relevant:

- `Kernel/Core/kernel_main.c` — Kernel entry and initialization
- `Kernel/Core/syscall/Syscall_Main.h` — Syscall number definitions
- `Kernel/Core/syscall/Syscall_Dispatch.c` — Syscall handler (~1300 lines)
- `Kernel/include/kernel/config.h` — Compile-time configuration
- `Kernel/include/kernel/status.h` — Error codes
- `Kernel/Drivers/Module/DriverBinary.h` — Driver API vtable
- `Kernel/Core/process/ProcessManager.h` — Process management API
- `Kernel/Core/vfs/VFS.h` — Virtual filesystem API
- `Kernel/IPC/IPC_Main.h` — IPC API
- `Kernel/Network/network_main.h` — Network stack API
- `Userland/Userland.c` — Init process
- `Userland/Syscalls.h` — Userland syscall aggregate header
- `Userland/POSIX/README_POSIX.md` — POSIX layer documentation
- `Makefile` — Top-level build system
- `Kernel/config/arch.mk` — Architecture-specific flags
