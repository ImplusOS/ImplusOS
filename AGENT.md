# ImplusOS — Agent Instructions

> This file provides instructions for AI coding agents working on ImplusOS.
> For tool-specific files, see also: `CLAUDE.md`, `GEMINI.md`.

## Project Overview

ImplusOS is a hobby operating system targeting **x86-64 (Long Mode)** with the following characteristics:

| Property | Value |
|---|---|
| Architecture | x86-64 (Long Mode) |
| Boot | UEFI via `gnu-efi` → ELF64 kernel |
| Kernel model | Monolithic with loadable driver modules (PIC ELF shared objects) |
| Languages | C (C11), NASM assembly (Intel syntax) |
| Filesystem | FAT32 (read/write) via VFS layer |
| Syscall ABI | AMD64 `SYSCALL` / `SYSRET` |
| Build | GNU Make + `x86_64-elf-gcc` cross compiler |
| Target | QEMU + OVMF |

## Repository Structure

```
ImplusOS/
├── BootLoader/        UEFI bootloader (Loader.c + boot logo resources)
├── Kernel/            Kernel source tree
│   ├── Kernel_Main.c  Entry point & boot sequence
│   ├── KernelConfig.h Compile-time configuration
│   ├── Common/        os_status_t error codes
│   ├── Memory/        Bitmap PMM + kernel heap + DMA allocator
│   ├── Paging/        4-level page tables (NX bit enabled)
│   ├── GDT/           Global Descriptor Table + TSS
│   ├── IDT/           Interrupt Descriptor Table
│   ├── SMP/           Multi-core support
│   ├── Syscall/       System call dispatch & handlers
│   ├── ProcessManager/ Process lifecycle, scheduling, capabilities
│   ├── Drivers/       Loadable driver modules (Client + Server)
│   ├── VFS/           Virtual File System layer
│   ├── IPC/           Inter-process message passing
│   ├── WindowManager/ Window manager kernel-side
│   ├── Network/       IPv4/UDP/TCP/ICMP/DHCP/DNS
│   ├── Ethernet/      Ethernet frame TX/RX
│   ├── ARP/           ARP resolution + cache
│   ├── IO/            Port I/O + disk abstraction (ATA, USB Mass Storage)
│   ├── ELF/           ELF loader (user process + driver modules)
│   ├── VMX/           Intel VMX virtualization support
│   ├── Sync/          Spinlock (TTAS with IRQ save/restore)
│   ├── Timer/         PIT + LAPIC timer
│   ├── Platform/      ACPI, LAPIC, IOAPIC, interrupt routing
│   └── Debbuger/      Serial, printf, panic handler
├── Userland/          User-space source tree
│   ├── Userland.c     Init process (_start)
│   ├── Syscalls.c/h   Raw syscall wrappers
│   ├── API/           High-level userland API headers
│   ├── Application/
│   │   ├── SystemApps/  System services (WM, Shell, etc.)
│   │   └── UserApps/    User applications
│   ├── DriverFramework/  Userland driver framework API
│   ├── NetworkStack/     Userland network utilities (DNS)
│   └── POSIX/            POSIX compatibility layer
├── libc/              Minimal C library (string, stdlib, stdio, math, errno)
├── Thirdparty/        Third-party code (stb_image, stb_truetype)
├── Docs/              Documentation
│   └── Architecture/  Technical reference documents
├── Makefile           Top-level build system
└── Doxyfile           Doxygen configuration
```

## ⚠️ MANDATORY: Read Rules & Workflows

Before making any changes, you **MUST** read the following files:

### Rules (`.agent/rules/`)

| File | Content |
|---|---|
| [coding-standards.md](.agent/rules/coding-standards.md) | Naming conventions, formatting, error handling, warnings |
| [architecture-constraints.md](.agent/rules/architecture-constraints.md) | Freestanding env, cross compiler, memory model, address space |
| [safety-critical.md](.agent/rules/safety-critical.md) | Interrupt safety, spinlocks, NX bit, syscall validation, SMP |

### Workflows (`.agent/workflows/`)

| File | Content |
|---|---|
| [build-and-test.md](.agent/workflows/build-and-test.md) | Build, QEMU run, debug, troubleshooting |
| [adding-syscall.md](.agent/workflows/adding-syscall.md) | How to add a new system call |
| [adding-driver-module.md](.agent/workflows/adding-driver-module.md) | How to add a loadable driver module |
| [adding-userland-app.md](.agent/workflows/adding-userland-app.md) | How to add a userland application |

## Architecture Documentation

For deeper understanding, refer to `Docs/Architecture/`:

| Document | Content |
|---|---|
| [Kernel_Architecture.md](Docs/Architecture/Kernel_Architecture.md) | Full kernel architecture overview |
| [Boot_Sequence.md](Docs/Architecture/Boot_Sequence.md) | Detailed boot flow reference |
| [Syscall_Reference.md](Docs/Architecture/Syscall_Reference.md) | System call numbers, arguments, return values |
| [Driver_Module_Guide.md](Docs/Architecture/Driver_Module_Guide.md) | How to create driver modules |
| [Kernel_Config_Guide.md](Docs/Architecture/Kernel_Config_Guide.md) | Compile-time configuration options |
| [Status_Codes.md](Docs/Architecture/Status_Codes.md) | Error code reference and errno mapping |
| [Repository_Structure.md](Docs/Architecture/Repository_Structure.md) | Detailed directory layout |

## Build Quick Reference

```bash
# Full build
make

# Clean build
make clean && make

# Create ISO image
make image_esp

# Run in QEMU (USB boot)
make run_usb

# Run in QEMU (IDE boot)
make run_ide
```

## Key Conventions Summary

### Error Handling

All kernel subsystems return `os_status_t` (`int64_t`). Negative values are errors.

```c
os_status_t result = some_function();
if (os_status_is_error(result)) {
    return result;  // propagate error
}
```

See `Kernel/Common/Status.h` for status codes.

### Naming

- Functions: `subsystem_action()` (e.g., `vfs_read_file()`, `process_create()`)
- Types: `snake_case_t` (e.g., `os_status_t`)
- Macros: `UPPER_SNAKE_CASE` with prefix (e.g., `OS_CONFIG_*`, `SYSCALL_*`)
- Files: `PascalCase` (e.g., `Kernel_Main.c`, `Memory_Main.c`)

### Safety

- Always validate pointers (NULL check) before use.
- Protect shared data with `spinlock_lock()` / `spinlock_unlock()` + IRQ save/restore.
- Never execute code from data pages (NX bit is enforced).
- Validate all user-supplied syscall arguments — never trust userland input.
