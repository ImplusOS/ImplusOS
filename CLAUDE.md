# CLAUDE.md — ImplusOS Project Guide for Claude Code

## Project Overview

ImplusOS is a hobby operating system targeting **x86-64 (Long Mode)** with a UEFI boot path.
It uses a monolithic kernel with loadable driver modules (PIC ELF shared objects),
a minimal C library, and userland applications that communicate with the kernel
via the AMD64 `SYSCALL`/`SYSRET` ABI.

## Repository Layout

```
ImplusOS/
├── BootLoader/           UEFI and BIOS bootloader entry points (x86_64/UEFI/, x86_64/BIOS/)
├── BootManager/          Secondary boot stage logic (UEFI and BIOS)
├── Kernel/               Kernel source (all subsystems)
│   ├── Arch/             Architecture-specific (x86_64, arm64)
│   ├── Boot/             Boot-time progress bar
│   ├── Core/             kernel_main and core subsystems (process, syscall, vfs, window, timer, sync, elf)
│   ├── Debug/            Serial output, printf, panic handler
│   ├── Drivers/          Driver module framework (Client/Server/Module)
│   ├── FileSystem/       FAT32 BPB definitions
│   ├── IPC/              Inter-process communication (ring-buffer message queues)
│   ├── MemoryManagement/ Physical memory (bitmap PMM), kernel heap, DMA allocator
│   ├── Network/          IPv4, ARP, DHCP, UDP, TCP, ICMP, Ethernet
│   ├── Platform/         ACPI, IOAPIC, LAPIC, interrupt routing, I/O subsystem
│   ├── config/           arch.mk (compiler/linker flags per architecture)
│   └── include/          Shared interfaces (arch_ops.h, driver_api.h, status.h, config.h, boot_info.h)
├── Userland/             User-space code
│   ├── API/              Syscall wrapper headers (File.h, Graphics.h, Input.h, etc.)
│   ├── Application/      System apps (shell, WM, version) + user apps (editor, clock, VM, etc.)
│   ├── DriverFramework/  Userland driver framework API
│   ├── NetworkStack/     DNS resolver
│   ├── POSIX/            POSIX compatibility layer (open, read, write, fork, socket, pthread, etc.)
│   ├── Syscalls.c/h      Unified syscall wrapper
│   ├── Userland.c        Init process (_start entry point)
│   └── Userland.ld       Linker script for userland ELFs
├── libc/I_libc/                 Minimal C library (string, stdlib, stdio, math, errno, POSIX shims)
├── Thirdparty/           stb_truetype.h, stb_image.h
├── Docs/                 Documentation and images
├── Makefile              Top-level build orchestrator
└── Doxyfile              Doxygen configuration
```

## Build System

### Toolchain Requirements

```bash
sudo apt install -y build-essential nasm binutils \
  gcc-x86-64-elf g++-x86-64-elf parted qemu-system-x86 \
  dosfstools xorriso mtools util-linux gdb
```

### Key Build Commands

| Command | Description |
|---|---|
| `make` | Build everything (bootloader, kernel, drivers, userland, apps) |
| `make kernel` | Build kernel only |
| `make app_build` | Build all userland applications |
| `make driver_build` | Build all driver modules |
| `make driver_stage` | Build drivers + copy ELFs to staging directory |
| `make image` | Build full hybrid ISO image (`Image/ImplusOS.iso`) |
| `make run_uefi_usb` | Launch QEMU with UEFI USB boot |
| `make run_uefi_cdrom`| Launch QEMU with UEFI CD-ROM boot |
| `make run_bios_usb` | Launch QEMU with BIOS USB boot |
| `make clean` | Remove all build artifacts |

### Build Artifacts

- `Build/Loader/BOOTX64.EFI` — UEFI loader shim
- `Build/BootManager/BOOTMANAGER.EFI` — Main UEFI boot manager
- `Build/BootManager/BootManager_BIOS.BIN` — BIOS boot manager
- `Build/Kernel/Kernel_Main.ELF` — Kernel binary
- `Build/Userland/Userland.ELF` — Init process
- `Build/Modules/<name>/<name>.ELF` — Driver modules
- `Build/Userland/SystemApps/<name>/<name>.ELF` — System applications
- `Build/Userland/UserApps/<name>/<name>.ELF` — User applications
- `Image/ImplusOS.iso` — Final bootable hybrid ISO

### Cross-Compiler

- Kernel: `x86_64-elf-gcc` with `-ffreestanding -fno-pic -mcmodel=large -mno-red-zone -nostdlib`
- Userland: `x86_64-elf-gcc` with `-ffreestanding -fno-pic -mcmodel=large -mno-red-zone -nostdlib`
- Bootloader: `x86_64-elf-gcc` with EDK2 includes, `-fpic -fshort-wchar`
- Assembly: `nasm -f elf64`
- Kernel linker: `x86_64-elf-ld -nostdlib -e kernel_main -T Arch/x86_64/linker/linker.ld`
- Userland linker: `x86_64-elf-ld -nostdlib -T Userland/Userland.ld`

## Architecture Key Details

### Kernel

- **Entry point**: `kernel_main()` in `Kernel/Core/kernel_main.c`
- **Initialization order**: serial → GDT → IDT → PMM → paging → heap → ACPI → interrupts → syscall → SMP → VMX → timer → drivers → FS → display → WM → process → IPC → network
- **Syscall ABI**: `SYSCALL`/`SYSRET` (AMD64). Syscall numbers in `Kernel/Core/syscall/Syscall_Main.h`. Dispatch in `Syscall_Dispatch.c`.
- **Error model**: All kernel subsystems return `os_status_t` (int64_t). Negative = error. See `Kernel/include/kernel/status.h`.
- **Configuration**: Compile-time `#define` constants in `Kernel/include/kernel/config.h` (max processes, max FDs, SMP CPUs, network config).
- **Process model**: Per-process CR3, capability-based security, round-robin scheduling.
- **IPC**: Ring-buffer message queues (max 256 bytes/message, 16 messages/process).
- **Driver modules**: PIC ELF shared objects loaded at boot. Driver API via `driver_binary_t` vtable. Modules export `driver_module_descriptor_t`.

### Userland

- **Init process**: `Userland/Userland.c` — spawns WindowManager, Shell, and user apps.
- **Syscall wrappers**: `Userland/API/*.h` headers provide typed C functions.
- **POSIX layer**: `Userland/POSIX/` maps ImplusOS syscalls to POSIX API (open, read, write, fork, socket, pthread, etc.).
- **Application convention**: Each app lives in `Userland/Application/{SystemApps,UserApps}/com_ImplusOS_<name>/` with its own `Makefile`.

### Memory Layout (User-Space)

| Region | Start | End |
|---|---|---|
| Code | `0x4000000000` | `0x4080000000` |
| Heap | `0x4100000000` | `0x47E0000000` |
| Stack | `0x47E0000000` | `0x4800000000` (32 MiB) |

## Coding Conventions

- **Language**: C (C11), with NASM assembly for arch-specific code.
- **Naming**: PascalCase for types/structs, snake_case for functions, UPPER_CASE for macros/constants.
- **Headers**: Use `#pragma once`. Include guards only in legacy headers.
- **Warnings**: `-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow`
- **No stdlib**: Kernel and userland are freestanding. Use the bundled `libc/I_libc/`.
- **Driver modules**: Must be position-independent (`-fPIC`). Export `driver_module_init()` symbol.

## Testing

- Primary testing via QEMU with OVMF firmware.
- Serial output (COM1, 115200 baud) is primary debug channel.
- `make run_usb` or `make run_ide` to boot in QEMU.
- QEMU config: q35 machine, 4 CPUs, 4GB RAM, VirtIO-Net, XHCI USB, NVMe storage.

## Important Files to Know

| File | Purpose |
|---|---|
| `Kernel/Core/kernel_main.c` | Kernel entry point and initialization sequence |
| `Kernel/Core/syscall/Syscall_Main.h` | All syscall numbers |
| `Kernel/Core/syscall/Syscall_Dispatch.c` | Syscall handler dispatch (giant switch) |
| `Kernel/include/kernel/config.h` | Compile-time kernel configuration |
| `Kernel/include/kernel/status.h` | OS error codes and errno mapping |
| `Kernel/Drivers/Module/DriverBinary.h` | Driver module API vtable definition |
| `Kernel/Core/process/ProcessManager.h` | Process management API + capabilities |
| `Userland/Userland.c` | Init process (spawns all initial processes) |
| `Userland/POSIX/README_POSIX.md` | POSIX compatibility layer documentation |
| `Kernel/config/arch.mk` | Architecture-specific compiler/linker flags |
