# AGENTS.md — ImplusOS Project Guide

*Last reviewed: 2026-08-29. Kept in sync with `CLAUDE.md` / `GEMINI.md`.*

## Project Overview

ImplusOS is a hobby operating system with a **monolithic kernel** and loadable
driver modules (position-independent ELF shared objects), a minimal freestanding
C library, and userland applications that reach the kernel through an
architecture-specific trap ABI.

- **Architectures**: `x86_64` (Long Mode) — primary, regularly booted in QEMU —
  and `arm64` (AArch64) — in progress. The default build target is `x86_64`;
  pass `ARCH=arm64` to target AArch64. The arm64 kernel link is currently
  blocked by a pre-existing freestanding-libc issue (`__trunctfdf2` from
  `vsnprintf`'s `long double` path); see `Docs/Others/TODO_OS_Refactor.md` §8.
- **Boot paths**: UEFI (both architectures, EDK2-based loader + boot manager)
  and legacy BIOS (x86_64 only).
- Development is expected to happen in an interactive Linux environment; builds
  may not complete in restricted/non-interactive containers.

## Repository Layout

```
ImplusOS/
├── BootLoader/            First-stage loader entry points
│   ├── x86_64/UEFI/       UEFI loader shim (EDK2 app)
│   ├── x86_64/BIOS/       BIOS loader (BiosLoader.c)
│   ├── arm64/UEFI/        AArch64 UEFI loader shim
│   └── Configuration/     EDK2 .dsc for the loader
├── BootManager/           Second-stage boot manager (loads Kernel_Main.ELF + driver/userland ELFs)
│   ├── Core/              ElfLoader, KASLR_RNG, platform abstraction
│   ├── UEFI/              UEFI entry + AArch64Trampoline.c (EL2→EL1)
│   ├── BIOS/              BIOS entry (stage2, real→long mode trampoline)
│   ├── BootManager_libc/  Tiny libc for the boot manager
│   └── Resource/          Boot logo + font staged onto the ESP
├── Kernel/
│   ├── Arch/{x86_64,arm64}/   cpu, hal, mmu, smp, timer, virt, interrupt, linker
│   ├── Boot/                  Boot progress bar (LoadBar)
│   ├── Compat/Linux/          Linux syscall-ABI compat layer + compat_registry
│   ├── Core/                  kernel_main + drm, elf, hardening, kvm, memory,
│   │                          process, sync, syscall, sysinfo, timer, usercopy, vfs
│   ├── Debug/                 serial, printf, panic
│   ├── Drivers/               Loadable driver modules + kernel-resident driver glue
│   │   ├── Audio/{AC97,HDA,VirtIOSound}
│   │   ├── Block/{AHCI,NVMe,VirtIOBlk}
│   │   ├── Bus/{PCI,USB/{OHCI,UHCI,EHCI,XHCI,HID,MassStorage}}
│   │   ├── Display/{ImplusOS_Generic,VirtIO}
│   │   ├── FileSystem/{FAT32,exFAT,ISO9660}
│   │   ├── Input/PS2
│   │   ├── NIC/{VirtIONet,I219V,WiFi}
│   │   ├── Wi-Fi/AX900          Firmware/AX900  RTC  Serial  ExampleDriver
│   │   ├── Manifest/DriverDB.txt  usb/pci VID→on-demand-module map
│   │   ├── Module/              DriverManager/BusRegistry/DeviceRegistry, *_VFS_Bridge,
│   │   │                        NetworkBuiltinDrivers, DriverSelect, resident bus gateways
│   │   └── module.mk            Shared build rules for driver modules
│   ├── IPC/                   Ring-buffer message queues + AF_UNIX (UnixSocket.c)
│   ├── MemoryManagement/      Bitmap PMM, kernel heap, DMA allocator
│   ├── Network/               ethernet, arp, ipv4, icmp, udp, tcp, dhcp, network_main
│   ├── Platform/              acpi, interrupt, io/Protocol/{AHCI,ATA,USB_MassStorage}, timer
│   ├── config/arch.mk         Per-architecture compiler/linker flags
│   └── include/               interfaces/arch_ops.h, kernel/{status.h,config.h,
│                              boot_info.h,interfaces/vfs_types.h}
├── Library/                 Shared source library, compiled into BOTH kernel and userland
│   ├── Crypto/              AES(-GCM/CBC/CTR), ChaCha20-Poly1305, SHA2, HMAC, HKDF,
│   │                        PBKDF2, RSA (PKCS1v15/PSS), ECDSA, ECDHE, Ed25519, X25519,
│   │                        X509(+verify), ASN1, Base64, Hex, CSPRNG, OCSP/CRL,
│   │                        TLS record layer + TLS 1.3 key schedule
│   ├── Unicode/{CP932,UTF8}
│   └── Identifier/UUID
├── Userland/
│   ├── API/                 Typed syscall wrappers (File.h, Graphics.h, Input.h,
│   │                        Socket.h, Audio.h, WiFi.h, OSDebug.h, ...) + helper
│   │                        libs (FreeType.c, Jpeg.c, XMLParser.c, Zlib.h)
│   ├── Application/         com.ImplusOS.windowmanager, com.ImplusOS.sysnotif, BusyBox
│   ├── Service/             Hot-loadable services, one ELF/.so each:
│   │                        com.ImplusOS.ldso (dynamic linker), com.ImplusOS.dynmain,
│   │                        com.ImplusOS.posix (POSIX layer), com.ImplusOS.netstack (DNS);
│   │                        services.list, service_client.[ch]
│   ├── Syscalls.c/.h        Unified syscall wrapper
│   ├── Userland.c           Init process (_start entry point)
│   └── Userland.ld          Linker script for userland ELFs
├── libc/I_libc/             Minimal freestanding C library
│   ├── include/             string.h, stdlib.h, stdio.h, math.h, errno.h + POSIX-ish
│   │                        headers (unistd.h, fcntl.h, pthread.h, sys/socket.h, ...)
│   └── src/sys/{x86_64,arm64}/hal_syscall.c   arch trap implementation
├── RecoveryEnvironment/     Recovery.c + Makefile (minimal init used by the install media)
├── Vendor/
│   ├── Header/              stb_truetype.h, stb_image.h
│   └── Library/             git submodules: libpng, zlib, libjpeg, freetype
├── Docs/                    Architecture/, Others/, Images/, OSS_License/
├── Makefile                 Top-level build orchestrator
└── Doxyfile                 Doxygen configuration
```

## Build System

### Toolchain

```bash
sudo apt install -y build-essential nasm binutils parted \
  qemu-system-x86 qemu-system-arm dosfstools xorriso mtools util-linux gdb

# Cross toolchains (Homebrew formulae are what the Makefile auto-detects;
# it looks under /home/linuxbrew/.linuxbrew on Linux, /opt/homebrew or
# /usr/local on macOS):
brew install x86_64-elf-gcc x86_64-elf-binutils
brew install aarch64-elf-gcc aarch64-elf-binutils   # only for ARCH=arm64
brew install gptfdisk lld
```

Building the UEFI EFI binaries additionally needs a checkout of
[EDK2](https://github.com/tianocore/edk2) at `$HOME/edk2` (override with
`EDK2_DIR=`), built once with `make -C BaseTools`. The Makefile drives EDK2 with
the `CLANGDWARF` toolchain (`EDK2_TOOLCHAIN=`, `EDK2_TARGET=RELEASE`).

### Key Build Commands

| Command | Description |
|---|---|
| `make` | Build everything for `ARCH` (bootloader shim, kernel, vendor libs, apps, services, drivers, init) |
| `make ARCH=arm64` | Build for AArch64 instead of x86_64 |
| `make kernel` | Build the kernel ELF only |
| `make app_build` | Build `Userland/Application/*` |
| `make service_build` | Build `Userland/Service/*` (posix, netstack, ldso, dynmain) |
| `make driver_build` | Build all driver modules |
| `make driver_stage` | Build drivers + copy `.ELF`s into the staging dir |
| `make recovery_build` | Build the recovery-environment init |
| `make vendor_libs` | Build the vendored libraries (libpng/zlib/libjpeg/freetype) |
| `make image` | Build the install-media hybrid ISO (`Image/ImplusOS-$(ARCH)-InstallMedia.iso`) |
| `make image_livecd` | Build the LiveCD hybrid ISO (`Image/ImplusOS-$(ARCH)-LiveCD.iso`) |
| `make run_uefi_usb` | Boot the LiveCD image in QEMU via UEFI, as a USB disk |
| `make run_uefi_cdrom` | Boot the LiveCD image in QEMU via UEFI, as a CD-ROM |
| `make run_bios_cdrom` | Boot the LiveCD image in QEMU via legacy BIOS (x86_64 only) |
| `make clean` | Remove `Build/` and `Image/` |

The `run_*` targets boot `Image/ImplusOS-$(ARCH)-LiveCD.iso`, so run
`make image_livecd` first (or run `make edk2_bootloader edk2_bootmanager`
once to produce the EFI binaries).

### Build Artifacts (`Build/$(ARCH)/`)

- `Build/$(ARCH)/Loader/BOOTX64.EFI` (or `BOOTAA64.EFI`) — UEFI loader shim
- `Build/$(ARCH)/BootManager/BOOTMANAGER.EFI` — UEFI boot manager
- `Build/$(ARCH)/Kernel/Kernel_Main.ELF` — kernel binary
- `Build/$(ARCH)/Userland/Userland.ELF` — init process
- `Build/Modules/$(ARCH)/<name>/<name>.ELF` — driver modules
- `Build/$(ARCH)/Userland/<app-or-service>/<name>.ELF` — apps and services
- `Image/ImplusOS-$(ARCH)-InstallMedia.iso`, `Image/ImplusOS-$(ARCH)-LiveCD.iso`

### Compiler / Linker Flags (`Kernel/config/arch.mk`, top-level `Makefile`)

- **Kernel**: `x86_64-elf-gcc -ffreestanding -fstack-protector-strong -fPIE
  -fno-plt -fno-builtin -nostdlib -nostartfiles -nodefaultlibs -mcmodel=small
  -mno-red-zone -O2 -DKERNEL -DPLATFORM_X86_64`
  (`-DPLATFORM_ARM64` + `-mstrict-align -mno-outline-atomics` for arm64).
- **Kernel link**: `x86_64-elf-ld -nostdlib --build-id=none -e kernel_main -pie
  --no-dynamic-linker -T Arch/x86_64/linker/linker.ld --gc-sections`
- **Userland**: freestanding, `-mcmodel=large -mno-red-zone` (x86_64) /
  `-mstrict-align -mno-outline-atomics` (arm64); linked with `Userland/Userland.ld`,
  entry `_start`.
- **Driver modules**: `-fPIC -shared`; each `Makefile` is just `include ../../module.mk`.
- **Warnings**: `-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow`.
  `make CI=1 ...` promotes them to `-Werror` (not clean tree-wide yet; see
  `Docs/Architecture/CI_CD.md` §3).

## Architecture Key Details

### Kernel

- **Entry point**: `kernel_main()` in `Kernel/Core/kernel_main.c` gets onto a
  known-good stack, then `kernel_main_after_stack_switch()` runs the
  **19 instrumented boot phases** (`boot_profile_begin/end`, dumped over serial).
  Canonical list: `Docs/Architecture/Boot_Sequence.md`. Summary order:
  `cpu_tables → pmm → paging → heap → acpi_interrupts → timer → syscall → smp →
  driver_module_load → driver_module_critical → input_init → disk_io_init →
  fs_init → display_init → process_manager → kernel_services → userland_elf →
  driver_module_deferred → audio_network_init`.
- **Arch abstraction**: `arch_ops_t` (`Kernel/include/interfaces/arch_ops.h`)
  hides x86_64/arm64 differences (`init_cpu_tables`, `get_timer_hal`,
  `virtualization_init`, `enter_user_mode`, ...). `kernel_main.c` has no
  `#ifdef PLATFORM_*` branches left for phase selection.
- **Syscall ABI**: x86_64 `SYSCALL`/`SYSRET` (num in `RAX`; args `RDI, RSI, RDX,
  R10, R8, R9`); arm64 `SVC #0` (num in `X8`; args `X0`–`X5`). Numbers in
  `Kernel/Core/syscall/Syscall_Main.h`; dispatch in `Syscall_Dispatch.c`.
- **Foreign ABIs**: `Kernel/Compat/` registers per-ABI syscall-number
  translators via `compat_registry`. `Kernel/Compat/Linux/` implements the Linux
  x86_64 ABI; a binary is routed there when `ELF_Loader.c` sees
  `EI_OSABI == ELFOSABI_LINUX`. `Syscall_Dispatch.c` contains no ABI-specific
  identifiers.
- **Error model**: kernel subsystems return `os_status_t` (int64_t); negative =
  error, mapping to errno via `os_status_to_errno()`. See
  `Kernel/include/kernel/status.h`. (Some older VFS/driver APIs still return
  `bool`; migration to `os_status_t` is incremental — `TODO_OS_Refactor.md` §7.)
- **Configuration**: compile-time `#define`s in `Kernel/include/kernel/config.h`.
- **Process model**: per-process address space (x86_64 CR3 / arm64 TTBR0_EL1),
  capability bitmask, round-robin scheduling with timer preemption.
- **Hardening**: `-fstack-protector-strong` in the kernel; `__stack_chk_guard`
  reseeded twice at boot from RDTSC / `CNTVCT_EL0`
  (`Kernel/Core/hardening/StackProtector.c`). KASLR RNG in the boot manager.
- **IPC**: ring-buffer message queues, 256 bytes/message,
  128 messages/process (`Kernel/IPC/IPC_Main.h`).
- **Driver modules**: PIC ELF shared objects. The kernel hands each a
  `driver_binary_t` vtable; the module exports `driver_module_init()` returning a
  `driver_module_descriptor_t`. Kernel-resident glue (bus gateways, VFS bridges)
  lives in `Kernel/Drivers/Module/`. See `Docs/Architecture/Driver_Module_Guide.md`.

### Kernel Configuration (`Kernel/include/kernel/config.h`, current defaults)

| Macro | Default | Notes |
|---|---|---|
| `OS_CONFIG_PROCESS_MAX_COUNT` | 256 | range 1–256 |
| `OS_CONFIG_FILE_MAX_FD` | 256 | per process, range 4–256 |
| `OS_CONFIG_FILE_MAX_DIR_HANDLE` | 256 | per process, range 4–256 |
| `OS_CONFIG_SMP_MAX_CPUS` | 16 | |
| `OS_CONFIG_SMP_ENABLED` | 1 | |
| `OS_CONFIG_DRIVER_MODULE_MAX_COUNT` | 64 | |
| `OS_CONFIG_SIGNAL_HANDLER_MAX_PER_PROCESS` | 32 | |
| `OS_CONFIG_PENDING_SIGNAL_MAX_PER_PROCESS` | 32 | |
| `OS_CONFIG_LOG_FILE_MAX_BYTES` | 512 KiB | |
| `OS_CONFIG_BOOT_FADE` | 0 | fade-to-black transition disabled |
| `OS_CONFIG_NET_IPV4_ADDR` / `MASK` / `GATEWAY` | 10.0.2.15 / 255.255.255.0 / 10.0.2.2 | QEMU user-net defaults |

### Process Capabilities (`Kernel/Core/process/ProcessManager.h`)

`SERIAL`, `PROCESS`, `FILE`, `MEMORY`, `INPUT`, `SIGNAL`, `IPC`, `NETWORK`,
`DISPLAY` (bits 0–8). `PROCESS_CAP_DEFAULT_MASK` grants all of them.

### Userland

- **Init process**: `Userland/Userland.c` (`_start`) — renders the boot screen,
  calls `service_load_all()` to load the services in
  `Userland/Service/services.list` (`com.ImplusOS.posix`, `com.ImplusOS.netstack`),
  spawns `com.ImplusOS.windowmanager`, waits for it to register, then spawns
  `com.ImplusOS.sysnotif`, then idles.
- **Applications** (`Userland/Application/`, reverse-domain names): the window
  manager (`com.ImplusOS.windowmanager` — compositor, decorations, scene graph,
  theme, IPC input routing), the notification daemon (`com.ImplusOS.sysnotif`),
  and `BusyBox`. Each has its own `Makefile` that pulls in `Userland/AppCommon.mk`.
  The window manager's launcher list is `.../windowmanager/Resource/Apps/apps.list`.
- **Services** (`Userland/Service/`): hot-loadable `.so`s managed by
  `service_client.h` (`service_load`/`service_unload`). `com.ImplusOS.posix` maps
  native syscalls to POSIX (open/read/write/fork/exec/socket/pthread/...);
  `com.ImplusOS.netstack` is the userland DNS resolver; `com.ImplusOS.ldso` +
  `com.ImplusOS.dynmain` provide dynamic linking.
- **Syscall wrappers**: `Userland/API/*.h` call `Userland/Syscalls.c`, which
  dispatches to `libc/I_libc/src/sys/$(ARCH)/hal_syscall.c`.

### Memory Layout (User-Space, x86_64)

| Region | Start | End |
|---|---|---|
| Code | `0x4000000000` | `0x4080000000` |
| Heap | `0x4100000000` | `0x47E0000000` |
| Stack | `0x47E0000000` | `0x4800000000` (32 MiB) |

arm64 uses different bases (see `Kernel/Arch/arm64/linker/linker.ld`).

## Coding Conventions

- **Language**: C11, plus NASM for x86_64 arch-specific assembly (`nasm -f elf64`).
  arm64 assembly is GAS (`.S`), built by the cross `gcc`.
- **Naming**: PascalCase for types/structs, snake_case for functions, UPPER_CASE
  for macros/constants.
- **Headers**: `#pragma once`.
- **Freestanding**: kernel and userland use `libc/I_libc/`, not a host libc.
- **Driver modules**: must be position-independent; export `driver_module_init()`.

## Testing

- Primary testing is a QEMU boot in q35 / OVMF (x86_64) or AAVMF (arm64), 4 GiB
  RAM, VirtIO-Net, XHCI USB, NVMe/AHCI storage.
- Serial (COM1, 115200 baud, mapped to `-serial stdio`) is the main debug
  channel; `boot_profile_dump("boot")` prints per-phase timings.
- There is no automated test suite in-tree. `Docs/Architecture/CI_CD.md`
  describes intended GitHub Actions workflows; the `.github/` directory is not
  currently committed.

## Important Files to Know

| File | Purpose |
|---|---|
| `Kernel/Core/kernel_main.c` | Kernel entry + 19-phase init sequence |
| `Kernel/include/interfaces/arch_ops.h` | x86_64/arm64 abstraction vtable |
| `Kernel/Core/syscall/Syscall_Main.h` | All native syscall numbers |
| `Kernel/Core/syscall/Syscall_Dispatch.c` | Native syscall dispatch (giant switch) |
| `Kernel/Compat/Linux/Syscall_LinuxCompat.c` | Linux syscall-ABI translation table |
| `Kernel/include/kernel/config.h` | Compile-time kernel configuration |
| `Kernel/include/kernel/status.h` | `os_status_t` codes + errno mapping |
| `Kernel/include/kernel/interfaces/vfs_types.h` | `vfs_driver_t` contract |
| `Kernel/Drivers/Module/DriverBinary.h` | Driver module API vtable |
| `Kernel/Core/process/ProcessManager.h` | Process API + capabilities |
| `Userland/Userland.c` | Init process |
| `Userland/Service/services.list` | Services loaded at init |
| `Userland/Service/com.ImplusOS.posix/README_POSIX.md` | POSIX layer docs |
| `Kernel/config/arch.mk` | Per-architecture compiler/linker flags |
| `Docs/Architecture/Boot_Sequence.md` | Canonical boot-phase reference |
| `Docs/Others/TODO_OS_Refactor.md` | Repo-wide refactor plan + status |
