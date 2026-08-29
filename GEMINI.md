# GEMINI.md — ImplusOS Project Guide for Gemini CLI

*Last reviewed: 2026-08-29. See `CLAUDE.md` / `AGENTS.md` for the long form.*

## Project Summary

ImplusOS is a hobby OS with a monolithic kernel + loadable driver modules and a
custom freestanding libc. Targets **x86_64** (primary, booted in QEMU) and
**arm64** (in progress). Boots via UEFI (both arches) or legacy BIOS (x86_64).
Default build arch is `x86_64`; use `ARCH=arm64` for AArch64.

## Quick Reference

### Build & Run

```bash
# Host tools (Ubuntu/Debian)
sudo apt install -y build-essential nasm binutils parted \
  qemu-system-x86 qemu-system-arm dosfstools xorriso mtools util-linux gdb
# Cross toolchain (Homebrew formulae; auto-detected under
# /home/linuxbrew/.linuxbrew on Linux)
brew install x86_64-elf-gcc x86_64-elf-binutils aarch64-elf-gcc aarch64-elf-binutils lld gptfdisk
# UEFI EFI binaries also need EDK2 checked out at $HOME/edk2 (make -C BaseTools once)

make                 # full build for ARCH (default x86_64)
make ARCH=arm64      # build for AArch64
make image           # install-media hybrid ISO  -> Image/ImplusOS-$(ARCH)-InstallMedia.iso
make image_livecd    # LiveCD hybrid ISO         -> Image/ImplusOS-$(ARCH)-LiveCD.iso
make run_uefi_usb    # boot LiveCD in QEMU, UEFI, USB disk
make run_uefi_cdrom  # boot LiveCD in QEMU, UEFI, CD-ROM
make run_bios_cdrom  # boot LiveCD in QEMU, legacy BIOS (x86_64 only)
make clean
```

Other targets: `kernel`, `app_build`, `service_build`, `driver_build`,
`driver_stage`, `recovery_build`, `vendor_libs`, `edk2_bootloader`,
`edk2_bootmanager`. The `run_*` targets boot the LiveCD image, so build it first.

### Toolchain

| Tool | x86_64 | arm64 |
|---|---|---|
| C compiler | `x86_64-elf-gcc` | `aarch64-elf-gcc` |
| Linker | `x86_64-elf-ld` | `aarch64-elf-ld` |
| Assembler | `nasm -f elf64` | GAS `.S` via `gcc` |
| UEFI (EDK2) | `CLANGDWARF` / `X64` | `CLANGDWARF` / `AARCH64` |
| QEMU firmware | `OVMF_CODE_4M.fd` | `AAVMF_CODE.fd` |

### Key Compiler Flags (`Kernel/config/arch.mk`)

- Kernel: `-ffreestanding -fstack-protector-strong -fPIE -fno-plt -fno-builtin
  -nostdlib -nostartfiles -nodefaultlibs -mcmodel=small -mno-red-zone -O2
  -DKERNEL -DPLATFORM_X86_64` (arm64: `-DPLATFORM_ARM64 -mstrict-align
  -mno-outline-atomics`)
- Kernel link: `-nostdlib -e kernel_main -pie --no-dynamic-linker -T
  Arch/$(ARCH)/linker/linker.ld --gc-sections`
- Userland: freestanding, `-mcmodel=large -mno-red-zone` (x86_64)
- Driver modules: `-fPIC -shared` (each `Makefile` is `include ../../module.mk`)
- Warnings: `-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion
  -Wshadow`; `make CI=1` → `-Werror`

## Repository Structure

```
ImplusOS/
├── BootLoader/            x86_64/UEFI, x86_64/BIOS, arm64/UEFI, Configuration/*.dsc
├── BootManager/           Core/ (ElfLoader, KASLR_RNG), UEFI/ (+AArch64Trampoline), BIOS/,
│                          BootManager_libc/, Resource/
├── Kernel/
│   ├── Arch/{x86_64,arm64}/   cpu, hal, mmu, smp, timer, virt, interrupt, linker
│   ├── Boot/                  boot progress bar (LoadBar)
│   ├── Compat/Linux/          Linux syscall-ABI compat layer + compat_registry
│   ├── Core/                  kernel_main + drm, elf, hardening, kvm, memory, process,
│   │                          sync, syscall, sysinfo, timer, usercopy, vfs
│   ├── Debug/                 serial, printf, panic
│   ├── Drivers/               Audio, Block, Bus/{PCI,USB}, Display, FileSystem/{FAT32,exFAT,
│   │                          ISO9660}, Input/PS2, NIC/{VirtIONet,I219V,WiFi}, Wi-Fi/AX900,
│   │                          Firmware, RTC, Serial, Manifest/DriverDB.txt,
│   │                          Module/ (DriverManager/BusRegistry/DeviceRegistry, *_VFS_Bridge,
│   │                          NetworkBuiltinDrivers, resident bus gateways), module.mk
│   ├── IPC/                   ring-buffer message queues + AF_UNIX (UnixSocket.c)
│   ├── MemoryManagement/      bitmap PMM, heap, DMA
│   ├── Network/               ethernet, arp, ipv4, icmp, udp, tcp, dhcp, network_main
│   ├── Platform/              acpi, interrupt, io/Protocol/{AHCI,ATA,USB_MassStorage}, timer
│   ├── config/arch.mk
│   └── include/               interfaces/arch_ops.h, kernel/{status.h,config.h,boot_info.h}
├── Library/                 shared source lib built into kernel AND userland:
│                            Crypto/ (AES, ChaCha20-Poly1305, SHA2, HMAC, HKDF, PBKDF2, RSA,
│                            ECDSA/ECDHE, Ed25519, X25519, X509+verify, ASN1, Base64, CSPRNG,
│                            TLS record + TLS1.3 key schedule), Unicode/{CP932,UTF8}, Identifier/UUID
├── Userland/
│   ├── API/                 typed syscall wrappers + FreeType.c/Jpeg.c/XMLParser.c helpers
│   ├── Application/          com.ImplusOS.windowmanager, com.ImplusOS.sysnotif, BusyBox
│   ├── Service/              hot-loadable: com.ImplusOS.{ldso,dynmain,posix,netstack};
│   │                        services.list, service_client.[ch]
│   ├── Syscalls.c/.h        unified syscall wrapper
│   ├── Userland.c           init process (_start)
│   └── Userland.ld
├── libc/I_libc/             freestanding libc; src/sys/{x86_64,arm64}/hal_syscall.c
├── RecoveryEnvironment/     Recovery.c + Makefile (used by the install media)
├── Vendor/                  Header/ (stb_truetype.h, stb_image.h), Library/ submodules
│                            (libpng, zlib, libjpeg, freetype)
├── Docs/                    Architecture/, Others/, Images/, OSS_License/
├── Makefile
└── Doxyfile
```

## Architecture Overview

### Boot Flow

1. UEFI firmware loads `BOOTX64.EFI`/`BOOTAA64.EFI` (loader shim) →
   `BOOTMANAGER.EFI` (boot manager). *Or* BIOS firmware loads the x86_64 BIOS
   stages → BIOS boot manager.
2. The boot manager sets up the framebuffer, loads `Kernel_Main.ELF` + driver
   ELFs + userland ELFs + font/logo, discovers ACPI RSDP and the boot
   partition, applies KASLR, and hands off `BOOT_INFO`.
3. UEFI path calls `ExitBootServices()`; both jump to `kernel_main()`.

### Kernel Initialization (19 phases; canonical list: `Docs/Architecture/Boot_Sequence.md`)

```
cpu_tables → pmm → paging → heap → acpi_interrupts → timer → syscall → smp →
driver_module_load → driver_module_critical → input_init → disk_io_init →
fs_init → display_init → process_manager → kernel_services → userland_elf →
driver_module_deferred → audio_network_init
```

`kernel_main.c` selects arch-specific behavior through `arch_ops_t`
(`Kernel/include/interfaces/arch_ops.h`), not `#ifdef`.

### Syscall Convention

- x86_64: `SYSCALL`/`SYSRET`; number in `RAX`; args `RDI, RSI, RDX, R10, R8, R9`;
  return in `RAX`.
- arm64: `SVC #0`; number in `X8`; args `X0`–`X5`; return in `X0`.
- Numbers: `Kernel/Core/syscall/Syscall_Main.h`. Dispatch: `Syscall_Dispatch.c`.
- Foreign ABIs go through `Kernel/Compat/` (`compat_registry` + per-ABI table);
  `Kernel/Compat/Linux/` implements the Linux x86_64 ABI, selected when
  `ELF_Loader.c` detects `EI_OSABI == ELFOSABI_LINUX`.

### Error Model

- Kernel functions return `os_status_t` (int64_t): `0`/positive = success,
  negative = error (`Kernel/include/kernel/status.h`, `os_status_to_errno()`).
- Some older VFS/driver APIs still return `bool`; the `os_status_t` migration is
  incremental.

### Process Model

- Up to `OS_CONFIG_PROCESS_MAX_COUNT` = **256** processes.
- Per-process address space (x86_64 CR3 / arm64 TTBR0_EL1).
- Capabilities: `SERIAL, PROCESS, FILE, MEMORY, INPUT, SIGNAL, IPC, NETWORK,
  DISPLAY` (`PROCESS_CAP_DEFAULT_MASK` grants all).
- Round-robin scheduling with timer-driven preemption.
- User memory (x86_64): Code `0x4000000000`, Heap `0x4100000000`,
  Stack `0x47E0000000`–`0x4800000000` (32 MiB).

### Driver Module System

- Drivers are PIC ELF shared objects loaded by the kernel at boot from the boot
  filesystem's `Kernel/Driver/` staging path.
- Kernel hands each module a `driver_binary_t` vtable (malloc/free, DMA, I/O
  ports, MMIO map, disk, PCI config, serial, timer). Modules export
  `driver_module_init(const driver_binary_t*)` → `driver_module_descriptor_t*`.
- Categories (`device_type_t`): PCI, USB host (OHCI/UHCI/EHCI/XHCI + HID +
  MassStorage), Filesystem (FAT32 rw, exFAT ro, ISO9660 ro), Display
  (ImplusOS_Generic FB, VirtIO-GPU), Input (PS/2), NIC (VirtIO-Net, I219V,
  AX900 Wi-Fi), Block (AHCI, NVMe, VirtIO-Blk), Audio (AC97, HDA, VirtIO-Sound).
- Kernel-resident glue (bus gateways, per-FS `*_VFS_Bridge.c`,
  `NetworkBuiltinDrivers.c`) lives in `Kernel/Drivers/Module/`.
- Runtime unload/reload via `driver_manager_unload_module()` /
  `driver_manager_reload_module()`.

### IPC

- Ring-buffer message queue per process: 256 bytes/message,
  128 messages/process (`Kernel/IPC/IPC_Main.h`).
- AF_UNIX sockets in `Kernel/IPC/UnixSocket.c`.

### Network Stack

- `Kernel/Network/`: Ethernet → ARP → IPv4 → {UDP, TCP, ICMP}; DHCP client.
- Each protocol layer is also registered into `DeviceRegistry` under
  `DEVICE_TYPE_NET_PROTOCOL` with a real vtable + `deps[]`
  (`Kernel/Drivers/Module/NetworkBuiltinDrivers.c`) — a hybrid step toward
  fully modular protocol drivers (`Docs/Architecture/Network_Stack.md`).
- Userland: Berkeley sockets via `com.ImplusOS.posix`; DNS via
  `com.ImplusOS.netstack`. TLS primitives live in `Library/Crypto/`.

### POSIX Compatibility (`Userland/Service/com.ImplusOS.posix/`)

Hot-loadable service mapping native syscalls to POSIX: file I/O (open, read,
write, lseek, stat, pipe, dup/dup2, mkdir, unlink, opendir/readdir), process
(getpid, fork, execve, waitpid, kill, _exit), signals (signal, sigaction,
sigprocmask, raise), threads (pthread create/join/mutex/cond/key), sockets
(socket, bind, connect, listen, accept, send/recv, AF_INET), time
(clock_gettime, nanosleep, gettimeofday, gmtime_r, mktime), memory (mmap), I/O
mux (select, poll, fcntl, ioctl). Details:
`Userland/Service/com.ImplusOS.posix/README_POSIX.md`.

## Coding Style

- C11 + NASM (x86_64) / GAS (arm64).
- PascalCase types, snake_case functions, UPPER_CASE macros. `#pragma once`.
- Freestanding: use `libc/I_libc/`, not a host libc.
- Warnings: `-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow`.

## Key Files

| File | Description |
|---|---|
| `Kernel/Core/kernel_main.c` | Kernel entry + 19-phase init |
| `Kernel/include/interfaces/arch_ops.h` | x86_64/arm64 abstraction vtable |
| `Kernel/Core/syscall/Syscall_Main.h` | Native syscall numbers |
| `Kernel/Core/syscall/Syscall_Dispatch.c` | Native syscall dispatch |
| `Kernel/Compat/Linux/Syscall_LinuxCompat.c` | Linux ABI translation table |
| `Kernel/include/kernel/config.h` | Compile-time kernel config |
| `Kernel/include/kernel/status.h` | Error codes + errno mapping |
| `Kernel/Drivers/Module/DriverBinary.h` | Driver API vtable |
| `Kernel/Core/process/ProcessManager.h` | Process API + capabilities |
| `Userland/Userland.c` | Init process |
| `Userland/Service/com.ImplusOS.posix/README_POSIX.md` | POSIX layer docs |
| `Docs/Architecture/Boot_Sequence.md` | Canonical boot-phase reference |

## Testing

- QEMU + OVMF (x86_64) / AAVMF (arm64); q35, 4 CPUs, 4 GiB RAM, VirtIO-Net,
  XHCI, NVMe/AHCI.
- Serial on COM1 (115200) via `-serial stdio`; `boot_profile_dump()` prints
  per-phase timings.
- No automated test suite in-tree. `Docs/Architecture/CI_CD.md` describes
  intended CI; `.github/` is not currently committed.
