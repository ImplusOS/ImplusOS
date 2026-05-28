# ImplusOS Repo

## Overview
ImplusOS is a hobby OS tree with a UEFI boot path, a small kernel, and userland samples.

- Build is intended for interactive Linux environments.
- Target host platform is Linux (Ubuntu and similar distributions) (Although we have confirmed the build on macOS, we cannot guarantee operation.).

<img width="960" height="540" alt="Qemu_10.2.0(macOS)" src="https://github.com/ImplusOS/ImplusOS/blob/a65f5e815ea0008a286dd81df7efdf719fb0208e/Docs/Images/Qemu_8.2.2(Debian).png" />
<img width="960" height="540" alt="ImplusOS" src="https://github.com/ImplusOS/ImplusOS/blob/c4619fce4ec78b6b1df997d764fe0ca9075f4eb2/Docs/Images/ImplusOS.png" />

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
![Static Badge](https://img.shields.io/badge/Repo-3.6MB-blue)
![Static Badge](https://img.shields.io/badge/Implus-OS-blue)

## Architecture at a Glance

| Component | Description |
|---|---|
| **Architecture** | x86-64 (Long Mode) |
| **Boot** | UEFI via `gnu-efi` → ELF64 kernel load |
| **Kernel model** | Monolithic with loadable driver modules (PIC ELF shared objects) |
| **Memory** | 4-level paging, bitmap PMM, kernel heap, DMA allocator |
| **Filesystem** | VFS layer → FAT32 (read / write / directory ops) |
| **Process model** | Per-process address space (CR3), capability-based, round-robin scheduling |
| **Syscall ABI** | AMD64 `SYSCALL` / `SYSRET` |
| **IPC** | Message-passing with per-process ring-buffer queues |
| **Display** | VirtIO-GPU and generic framebuffer with double-buffering |
| **Input** | PS/2 + USB HID (keyboard and mouse) |
| **Networking** | Ethernet → ARP → IPv4 → UDP / TCP / DNS / DHCP / ICMP (VirtIO-Net) |
| **Debug** | COM1 serial (115200), kernel printf, panic handler |

```
┌─────────────────────────────────────────────────────┐
│                    Userland (Ring 3)                 │
│  Init → WindowManager, MouseManager, GUI Demo, Apps │
├────────────────────── SYSCALL ──────────────────────┤
│                     Kernel (Ring 0)                  │
│  ┌──────────┐ ┌────────┐ ┌─────┐ ┌──────┐ ┌──────┐ │
│  │ Process  │ │ VFS /  │ │ IPC │ │ WM   │ │ Net  │ │
│  │ Manager  │ │ FAT32  │ │     │ │Kernel│ │Stack │ │
│  └──────────┘ └────────┘ └─────┘ └──────┘ └──────┘ │
│  ┌──────────────────────────────────────────────────┐│
│  │            Driver Module Manager                 ││
│  │  PCI │ FAT32 │ PS2 │ USB │ VirtIO │ Display     ││
│  └──────────────────────────────────────────────────┘│
│  ┌──────────┐ ┌────────┐ ┌─────┐ ┌──────┐ ┌──────┐ │
│  │ Memory / │ │  GDT / │ │ SMP │ │ ACPI │ │Timer │ │
│  │ Paging   │ │  IDT   │ │     │ │ APIC │ │      │ │
│  └──────────┘ └────────┘ └─────┘ └──────┘ └──────┘ │
├─────────────────────────────────────────────────────┤
│              UEFI Bootloader (Loader.c)             │
│  GOP setup → BMP logo → ELF load → Driver preload  │
└─────────────────────────────────────────────────────┘
```

## Build
1. Install toolchains:
```bash
sudo apt install -y build-essential pkg-config git make cmake
sudo apt install -y gcc-multilib g++-multilib
sudo apt install -y nasm
sudo apt install -y binutils
sudo apt install -y gnu-efi
sudo apt install -y parted
sudo apt install -y qemu-system-x86
sudo apt install -y gdb
sudo apt install -y dosfstools
sudo apt install -y xorriso
sudo apt install -y mtools
sudo apt install -y util-linux
# For macOS (Homebrew)
brew install x86_64-elf-binutils
brew install x86_64-elf-gcc
```
2. Build and run:
```bash
make
make run_uefi_usb   # UEFI USB boot
make run_uefi_cdrom # UEFI CD-ROM boot
make run_bios_usb   # BIOS USB boot
```

## Notes
* This project assumes building in an interactive Linux environment (such as a local terminal).
* You may not be able to build successfully in non-interactive environments (CI/CD, restricted container environments, etc.).

## Current Feature Set
- UEFI and BIOS boot paths.
- Process manager and syscall dispatch.
- FAT32 file I/O syscall backend.
- PS/2 keyboard and mouse input path.
- USB host controller stack (OHCI, UHCI, EHCI, XHCI) with HID and Mass Storage class drivers.
- Window manager with modern design (smooth layouts, enhanced decorations).
- Desktop subsystem with background rendering and icon management.
- PNG decoder user application sample.
- Display drivers: VirtIO GPU, generic framebuffer with double-buffering support.
- Network stack: Ethernet, ARP, IPv4, UDP, TCP, DNS, DHCP, ICMP over VirtIO-Net.
- Inter-process communication via message-passing queues.
- Capability-based process security model.
- SMP support with TLB shootdown.
- NX (No-Execute) bit paging support for enhanced security.
- Berkeley-style Socket API support in userland.
- XML Parser utility library in userland.

## Current Constraints
- Verified operation is QEMU + OVMF centric (for UEFI).
- Physical hardware operation is not guaranteed.
- Audio drivers are not yet integrated.

## Error and Status Contract
- Kernel subsystems return `os_status_t` (`Kernel/include/kernel/status.h`).
- Negative status values are errors.
- Userland wrappers expose `os_errno` with status-to-errno conversion.

## Documentation
- Architecture references:
  - `Docs/Architecture/Kernel_Architecture.md` — Full kernel architecture overview
  - `Docs/Architecture/Driver_Module_Guide.md` — How to create driver modules
  - `Docs/Architecture/Userland_Specification.md` — Userland app development guide
- API docs generation:
  - `doxygen Doxyfile`

## Repository Structure
```
ImplusOS/
├── BootLoader/        UEFI/BIOS entry points (x86_64/UEFI/, x86_64/BIOS/)
├── BootManager/       Secondary boot stage logic (UEFI/BIOS)
├── Kernel/            Kernel source (all subsystems)
│   ├── Arch/          Architecture-specific (GDT, IDT, mmu, SMP, virt)
│   ├── Core/          Core subsystems (process, syscall, vfs, window, timer, sync)
│   ├── Debug/         Serial, printf, panic
│   ├── Drivers/       Loadable driver modules (PCI, FAT32, PS2, USB, Display, NIC)
│   ├── MemoryManagement/ Physical + virtual memory management (PMM, heap)
│   ├── IPC/           Inter-process communication
│   ├── Network/       IPv4/UDP/TCP/ICMP/DHCP network stack
│   └── Platform/      ACPI, IOAPIC, LAPIC, I/O protocols
├── Userland/          User-space init + applications
│   ├── API/           Userland syscall wrapper headers
│   └── Application/   System and user applications
├── libc/              Minimal C library (string, stdlib, stdio, math)
├── Docs/              Documentation (architecture, images)
├── Makefile           Top-level build system
└── Doxyfile           Doxygen configuration
```

## License
MIT.

## Thirdparty
- This repository is using thridparty code.
  - `/Thirdparty`
