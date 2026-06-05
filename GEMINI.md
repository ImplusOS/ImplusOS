# GEMINI.md — ImplusOS Project Guide for Gemini CLI

## Project Summary

ImplusOS is a hobby operating system for x86-64 with UEFI boot.
Monolithic kernel + loadable driver modules. Custom libc.
Runs on QEMU with OVMF firmware.

## Quick Reference

### Build & Run

```bash
# Prerequisites (Ubuntu/Debian)
sudo apt install -y build-essential nasm binutils \
  gcc-x86-64-elf g++-x86-64-elf parted qemu-system-x86 \
  dosfstools xorriso mtools util-linux gdb

# Full build
make

# Create bootable hybrid ISO (UEFI + BIOS)
make image

# Run in QEMU
make run_uefi_usb   # UEFI USB boot
make run_uefi_cdrom # UEFI CD-ROM boot
make run_bios_usb   # BIOS USB boot

# Clean
make clean
```

### Toolchain

| Tool | Binary |
|---|---|
| C Compiler | `x86_64-elf-gcc` |
| Linker | `x86_64-elf-ld` |
| Assembler | `nasm` (x86_64 only) |

### Key Compiler Flags

- Kernel: `-ffreestanding -fno-pic -mcmodel=large -mno-red-zone -nostdlib -DKERNEL`
- Userland: `-ffreestanding -fno-pic -mcmodel=large -mno-red-zone -nostdlib`
- Drivers: `-fPIC -shared` (position-independent ELF shared objects)

## Repository Structure

```
ImplusOS/
├── BootLoader/           # UEFI/BIOS entry points (x86_64/UEFI/, x86_64/BIOS/)
├── BootManager/          # Secondary boot stage (UEFI/BIOS)
├── Kernel/               # Kernel source
│   ├── Arch/             Architecture-specific (x86_64, arm64)
│   ├── Core/             # kernel_main, process, syscall, VFS, IPC, timer, window, sync, elf
│   ├── Debug/            # Serial, printf, panic
│   ├── Drivers/          # Driver framework (Client/Server/Module)
│   ├── IPC/              # Message-passing IPC
│   ├── MemoryManagement/ # PMM (bitmap), heap, DMA
│   ├── Network/          # IPv4/ARP/DHCP/UDP/TCP/ICMP/Ethernet
│   ├── Platform/         # ACPI, IOAPIC, LAPIC, I/O protocols
│   ├── config/           # arch.mk
│   └── include/          # Shared headers (status.h, config.h, interfaces/)
├── Userland/             # User-space
│   ├── API/              # Syscall wrapper headers
│   ├── Application/      # System and user applications
│   ├── POSIX/            # POSIX compatibility layer
│   ├── Syscalls.c/h      # Unified syscall wrappers
│   ├── Userland.c        # Init process
│   └── Userland.ld       # Linker script
├── libc/                 # Minimal C library
├── Thirdparty/           # stb_truetype.h, stb_image.h
├── Makefile              # Top-level build
└── Doxyfile              # Doxygen config
```

## Architecture Overview

### Boot Flow

1. UEFI firmware loads `BOOTX64.EFI` (shim) → `BOOTMANAGER.EFI` (main manager)
   OR BIOS firmware loads `stage1.bin` → `stage2.bin` → `BootManager_BIOS.BIN`
2. BootManager sets up GOP framebuffer (UEFI) or VESA (BIOS), loads kernel ELF + driver ELFs + font data
3. Discovers ACPI RSDP, partition BPB, boot drive type
4. Exits Boot Services (UEFI), jumps to `kernel_main()`

### Kernel Initialization (in order)

```
serial_init → GDT → IDT → PMM → paging → heap → ACPI → interrupts →
syscall_init → SMP → VMX → timer → driver modules → filesystem → display →
window manager → process manager → IPC → network → launch Userland.ELF
```

### Syscall Convention

- AMD64 `SYSCALL`/`SYSRET` instruction pair
- Syscall number in `RAX`
- Arguments: `RDI`, `RSI`, `RDX`, `R10`, `R8`
- Return value in `RAX`
- All syscall numbers defined in `Kernel/Core/syscall/Syscall_Main.h`

### Error Model

- Kernel functions return `os_status_t` (int64_t)
- `0` = success, negative = error
- Status codes: `OS_STATUS_OK`, `OS_STATUS_INVALID_ARG` (-22), `OS_STATUS_NOT_FOUND` (-2), etc.
- Defined in `Kernel/include/kernel/status.h`

### Process Model

- Max 128 processes (configurable via `OS_CONFIG_PROCESS_MAX_COUNT`)
- Per-process CR3 (separate address spaces)
- Capability-based security: SERIAL, PROCESS, FILE, MEMORY, INPUT, SIGNAL, IPC, NETWORK
- Round-robin scheduling with timer-driven preemption
- User-space memory layout: Code @ `0x4000000000`, Heap @ `0x4100000000`, Stack @ `0x4800000000` (32 MiB)

### Driver Module System

- Drivers are PIC ELF shared objects loaded by the kernel at boot
- Kernel provides `driver_binary_t` vtable (malloc, free, I/O ports, DMA, disk, PCI, serial)
- Drivers export `driver_module_init(const driver_binary_t *api)` → returns `driver_module_descriptor_t*`
- Driver types: PCI, FAT32, Display (VirtIO-GPU / Generic FB), PS2, USB (OHCI/UHCI/EHCI/XHCI), NIC (VirtIO-Net)

### IPC

- Ring-buffer message queues per process
- Max 256 bytes/message, 16 messages/queue
- API: `ipc_send_message()`, `ipc_receive_message()`

### Network Stack

- Layers: Ethernet → ARP → IPv4 → UDP/TCP/ICMP
- DHCP client, DNS resolver (userland)
- Hardware: VirtIO-Net NIC driver
- Userland: Berkeley socket API via POSIX layer

### POSIX Compatibility

Full POSIX layer in `Userland/POSIX/` providing:
- File I/O: open, read, write, close, lseek, stat, pipe, dup/dup2, mkdir, unlink, opendir/readdir
- Process: getpid, fork, exec, waitpid, kill, _exit
- Signals: signal, sigaction, sigprocmask, raise
- Threads: pthread_create/join/mutex/cond/key
- Networking: socket, bind, connect, listen, accept, send, recv
- Time: clock_gettime, nanosleep, gettimeofday, gmtime_r, mktime
- Memory: mmap
- I/O: select, poll, fcntl, ioctl

## Coding Style

- **Language**: C (C11) + NASM assembly
- **Naming**: PascalCase types, snake_case functions, UPPER_CASE macros
- **Headers**: `#pragma once`
- **No stdlib**: Freestanding environment. Use bundled `libc/`.
- **Warnings**: `-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow`

## Key Files

| File | Description |
|---|---|
| `Kernel/Core/kernel_main.c` | Kernel entry + init sequence |
| `Kernel/Core/syscall/Syscall_Main.h` | Syscall number definitions |
| `Kernel/Core/syscall/Syscall_Dispatch.c` | Syscall dispatch (main switch) |
| `Kernel/include/kernel/config.h` | Compile-time kernel config |
| `Kernel/include/kernel/status.h` | Error codes + errno mapping |
| `Kernel/Drivers/Module/DriverBinary.h` | Driver API vtable |
| `Kernel/Core/process/ProcessManager.h` | Process API + capabilities |
| `Userland/Userland.c` | Init process |
| `Userland/POSIX/README_POSIX.md` | POSIX layer docs |

## Testing

- QEMU + OVMF (q35, 4 CPUs, 4GB RAM, VirtIO-Net, XHCI)
- Serial output on COM1 (115200 baud) via `-serial stdio`
- No automated test suite — manual QEMU testing
