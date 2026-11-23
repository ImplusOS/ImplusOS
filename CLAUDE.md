# CLAUDE.md — ImplusOS Project Guide for Claude Code

*Last reviewed: 2026-08-29*

## Project Overview

ImplusOS is a hobby operating system with a **monolithic kernel** and loadable
driver modules (position-independent ELF shared objects), a minimal freestanding
C library, and userland applications that reach the kernel through an
architecture-specific trap ABI.

- **Architectures**: `x86_64` (Long Mode) — primary, regularly booted in QEMU —
  and `arm64` (AArch64) — boots to the desktop in QEMU `virt` (TCG and macOS
  HVF; SMP via PSCI, GICv2/v3). Device registers on arm64 go through
  `Kernel/Source/include/interfaces/mmio.h` (HVF cannot replay writeback MMIO). The default build target is `x86_64`; pass `ARCH=arm64` to
  target AArch64. The bundled foreign Linux binaries (Xorg/xterm, Chromium,
  dash, Wine, ...) are x86_64 builds; the arm64 image bundles the same
  x86_64 runtime and runs it under the translator (below), slowly.
  The kernel itself runs three Linux ABIs: x86_64 and i386 (32-bit
  compatibility mode, `int $0x80`) on x86_64, AArch64 (PIE only) on arm64 --
  see `Docs/Architecture/Compat_Layers.md` §6. A Linux ELF for the *other*
  CPU (x86_64 on arm64, AArch64 on x86_64) is run by the user-mode CPU
  translator `Userland/Application/Translator` (interpreter + syscall
  translation, qemu-user style), which exec/spawn start in its place --
  `Compat_Layers.md` §7.
  arm64 userland links libgcc for binary128 `long double` soft-fp.
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
│   │                          process, sound (ALSA ABI + mixer), sync, syscall,
│   │                          sysinfo, timer, usercopy, vfs
│   ├── Debug/                 serial, printf, panic
│   ├── Drivers/               Loadable driver modules + kernel-resident driver glue
│   │   ├── Audio/{AC97,HDA,VirtIOSound}
│   │   ├── Block/{AHCI,NVMe,VirtIOBlk}
│   │   ├── Bus/{PCI,USB/{OHCI,UHCI,EHCI,XHCI,HID,MassStorage}}
│   │   ├── Display/{ImplusOS_Generic,VirtIO}
│   │   ├── FileSystem/{FAT32,exFAT,ISO9660,UDF}
│   │   ├── Input/PS2
│   │   ├── NIC/{VirtIONet,I219V,WiFi}
│   │   ├── Wi-Fi/AIC8800        Firmware/AIC8800  RTC  Serial  ExampleDriver
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
│   │                        libs (FreeType.c), the UI toolkit (UI.c) and desktop theme (Theme.c)
│   ├── Application/         com.ImplusOS.windowmanager, com.ImplusOS.sysnotif, BusyBox,
│   │                        Translator (x86_64 <-> AArch64 Linux program translator)
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
│   └── Library/             freetype (built by Vendor/Library/Makefile)
├── Docs/                    Architecture/, Others/, Images/, OSS_License/
├── Makefile                 Top-level build orchestrator
└── Doxyfile                 Doxygen configuration
```

## Build System

### Toolchain

```bash
sudo apt install -y build-essential nasm binutils parted \
  qemu-system-x86 qemu-system-arm dosfstools xorriso genisoimage mtools util-linux gdb

# Cross toolchains (Homebrew formulae are what the Makefile auto-detects;
# it looks under /home/linuxbrew/.linuxbrew on Linux, /opt/homebrew or
# /usr/local on macOS):
brew install x86_64-elf-gcc x86_64-elf-binutils
brew install aarch64-elf-gcc aarch64-elf-binutils   # only for ARCH=arm64
brew install gptfdisk lld
```

**macOS hosts**: run the build with `gmake` (`brew install make`; the
system `/usr/bin/make` is 3.81 and is rejected). Also `brew install nasm
mtools cdrtools qemu llvm lld glib shared-mime-info` — cdrtools' `mkisofs` stands in for
`genisoimage`, and `lld` must be the same version as `llvm` or the EDK2 LTO
link crashes. `make run_*` uses TCG + cocoa + Homebrew's EDK2 firmware there,
and Core Audio (`-audiodev coreaudio` + `hda-output`) for sound. Homebrew's
QEMU has no virgl (`virtio-gpu-gl-pci`), so the guest never reports a 3D GPU
and Chromium runs without GPU acceleration (WebGL unavailable) on a Mac --
`Docs/Others/TODO_Chromium_LinuxABI.md` §21. UTM does have virgl (ANGLE),
but its host offers no multisample textures, so Mesa caps at ES 3.0 with
GL_MAX_SAMPLES 1, ANGLE at ES 2.0, and Chromium (ES 3.0 required) still
falls back to software (§23). When `milesbuckton/qemu-virgl` is installed,
arm64 `run_*` use it with `QEMU_GPU=virgl` (`virtio-gpu-gl-pci`,
`cocoa,gl=es`); its host GL is the same ANGLE ES 3.0, so the same applies
-- the host-side virglrenderer bugs behind it are in §25.
`linux_runtime_stage` works but cannot run Linux binaries on the host, so it
stages no glibc C.UTF-8 locale-archive and no gdk-pixbuf `loaders.cache`
(and no `mime.cache` unless `update-mime-database` is installed); it logs a
WARN for each. `mime.cache` is not optional: without it gdk-pixbuf cannot
tell a PNG from anything else and every GTK3 program (gtk3-demo,
gtk3-widget-factory, gtk3-icon-browser) aborts on its first icon --
Homebrew's `shared-mime-info` provides the `update-mime-database` the
staging falls back to. (PNG/JPEG are built into Debian's gdk-pixbuf, so the
missing `loaders.cache` only costs the rarer formats.)

Building the UEFI EFI binaries additionally needs a checkout of
[EDK2](https://github.com/tianocore/edk2) at `$HOME/edk2` (override with
`EDK2_DIR=`), built once with `make -C BaseTools`. The Makefile drives EDK2 with
the `CLANGDWARF` toolchain (`EDK2_TOOLCHAIN=`, `EDK2_TARGET=RELEASE`).

### Key Build Commands

| Command | Description |
|---|---|
| `make` | Build everything for `ARCH` (bootloader shim, kernel, vendor lib, apps, drivers, init) |
| `make ARCH=arm64` | Build for AArch64 instead of x86_64 |
| `make kernel` | Build the kernel ELF only |
| `make app_build` | Build `Userland/Application/*` |
| `make driver_build` | Build all driver modules |
| `make driver_stage` | Build drivers + copy `.ELF`s into the staging dir |
| `make recovery_build` | Build the recovery-environment init |
| `make vendor_libs` | Build the vendored library (freetype) |
| `make image` | Build the install-media UDF-bridge ISO (`Image/ImplusOS-$(ARCH)-InstallMedia.iso`) via `genisoimage -udf` |
| `make image_livecd` | Build the LiveCD UDF-bridge ISO (`Image/ImplusOS-$(ARCH)-LiveCD.iso`) via `genisoimage -udf` |
| `make image_livecd ARCH=arm64 LINUX_RUNTIME_TRANSLATED=0` | arm64 image without the (translated) x86_64 Linux runtime |
| `make run_uefi_usb` | Boot the LiveCD image in QEMU via UEFI, as a USB disk |
| `make run_uefi_cdrom` | Boot the LiveCD image in QEMU via UEFI, as a CD-ROM |
| `make run_bios_cdrom` | Boot the LiveCD image in QEMU via legacy BIOS (x86_64 only) |
| `make run_uefi_cdrom QEMU_GPU=virgl` | Same, with a virtio-gpu-gl device: Xorg/glamor and Chromium render on the host GPU (see `Docs/Others/TODO_Chromium_LinuxABI.md` §16) |
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
- `Build/$(ARCH)/Userland/<app>/<name>.ELF` — apps
- `Image/ImplusOS-$(ARCH)-InstallMedia.iso`, `Image/ImplusOS-$(ARCH)-LiveCD.iso`

### Compiler / Linker Flags (`Kernel/config/arch.mk`, top-level `Makefile`)

- **Kernel**: `x86_64-elf-gcc -ffreestanding -fstack-protector-strong -fPIE
  -fno-plt -fno-builtin -nostdlib -nostartfiles -nodefaultlibs -mcmodel=small
  -mno-red-zone -O2 -DKERNEL -DPLATFORM_X86_64`
  (`-DPLATFORM_ARM64` + `-mstrict-align -mno-outline-atomics
  -mgeneral-regs-only` for arm64; driver modules use the same).
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
  translators via `compat_registry`. `Kernel/Compat/Linux/` implements Linux
  once in the x86_64 numbering (`Syscall_LinuxCompat.c`); `i386/` (x86_64
  kernel, `int $0x80`, ILP32) and `arm64/` (arm64 kernel, generic numbering)
  are front ends that translate into it through `linux_syscall_core()`
  (`Linux_CompatCore.h`). `ELF_Loader.c` picks `process_t.abi_mode`
  (`PROCESS_ABI_LINUX`, `_LINUX_I386` for ELFCLASS32 EM_386, `_LINUX_ARM64`);
  `PROCESS_ABI_IS_LINUX()` covers all three. i386 programs live in a reserved
  low window (`OS_CONFIG_LINUX_I386_WINDOW_*`, 256 MiB of RAM withheld at
  boot). Details: `Docs/Architecture/Compat_Layers.md` §6.
- **Foreign CPUs**: exec/spawn of an ELFCLASS64 Linux binary for the other
  CPU loads `OS_CONFIG_TRANSLATOR_DIR/<x86_64|aarch64>.ELF` instead
  (`process_foreign_translator()` in `ProcessManager_Create.c`), with argv
  `{translator, "-0", argv0, path, argv1..}`; the process keeps the guest's
  name and exe path. The translator is a static host-Linux-ABI program that
  loads and runs the guest: x86_64 guests on arm64 through a JIT to AArch64
  code (`guest/x86_64/x86_jit.c`; it needs EL0 cache maintenance, which the
  arm64 kernel enables via `SCTLR_EL1.UCI`/`UCT`), AArch64 guests on x86_64
  through a uop block engine. Details: `Compat_Layers.md` §7 and
  `Userland/Application/Translator/README.md`.
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
| `OS_CONFIG_PROCESS_MAX_COUNT` | 512 | tasks (every thread takes a slot), range 1–1024 |
| `OS_CONFIG_FILE_MAX_FD` | 2048 | system-wide (global) fd table, range 4–4096. 512–1023 is skipped (inet 512–767, AF_UNIX 768–1023); numbers ≥1024 go only to Linux-ABI processes, which see per-process numbers (`Compat/Linux/Linux_FdTable.c`) |
| `OS_CONFIG_FILE_MAX_DIR_HANDLE` | 192 | range 4–256 |
| `OS_CONFIG_SMP_MAX_CPUS` | 16 | |
| `OS_CONFIG_SMP_ENABLED` | 1 | |
| `OS_CONFIG_DRIVER_MODULE_MAX_COUNT` | 64 | |
| `OS_CONFIG_SIGNAL_HANDLER_MAX_PER_PROCESS` | 32 | |
| `OS_CONFIG_PENDING_SIGNAL_MAX_PER_PROCESS` | 32 | |
| `OS_CONFIG_LOG_FILE_MAX_BYTES` | 512 KiB | |
| `OS_CONFIG_BOOT_FADE` | 0 | fade-to-black transition disabled |
| `KERNEL_COW_FORK` | 1 | copy-on-write fork on both architectures (needed by multi-process Chromium); `-DKERNEL_COW_FORK=0` for eager copy |
| `OS_CONFIG_NET_IPV4_ADDR` / `MASK` / `GATEWAY` | 10.0.2.15 / 255.255.255.0 / 10.0.2.2 | QEMU user-net defaults |
| `OS_CONFIG_LINUX_I386_WINDOW_BASE` / `_SIZE` | 0x08000000 / 256 MiB | x86_64: address window for Linux i386 programs; its RAM is withheld at boot (0 disables) |
| `OS_CONFIG_TRANSLATOR_DIR` | `/Userland/Translator` | where exec/spawn look for `<isa>.ELF` to run a Linux program for the other CPU (`""` disables) |

### Process Capabilities (`Kernel/Core/process/ProcessManager.h`)

`SERIAL`, `PROCESS`, `FILE`, `MEMORY`, `INPUT`, `SIGNAL`, `IPC`, `NETWORK`,
`DISPLAY` (bits 0–8). `PROCESS_CAP_DEFAULT_MASK` grants all of them.

### Userland

- **Init process**: `Userland/Source/Userland.c` (`_start`) — renders the boot
  screen, then spawns `com.ImplusOS.loginui`, which owns the
  rest of the session (falls back to launching the window manager directly if
  the login app is missing from the image).
- **Login screen**: `com.ImplusOS.loginui` — graphical, drawn straight to the
  framebuffer before the WM exists. Paints the WM wallpaper, lists users
  (icon + name) bottom-left with per-row hit-testing and no panel behind the
  section, shows a create-user form when the store is empty. Credential store
  is entirely userland: `/var/System/users.db` (writable tmpfs), one
  `name:salt_hex:hash_hex` line per user, hash = PBKDF2-HMAC-SHA256 via
  `Library/Crypto`. uid = 1000 + record index; the chosen session is written
  to `/run/session.user` + `/run/session.uid`. Optional unattended login via
  `/var/System/autologin` or the shipped `Resource/autologin.conf` seed
  (`name` or `name:password`; disabled by default). On success it starts the
  window-manager session and stays resident as the session leader.
- **Applications** (`Userland/Application/`, reverse-domain names): the window
  manager (`com.ImplusOS.windowmanager` — compositor, decorations, scene graph,
  theme, IPC input routing), the notification daemon (`com.ImplusOS.sysnotif`),
  the login screen (`com.ImplusOS.loginui`), `BusyBox`, `XdgOpen` (staged
  as `/usr/bin/xdg-open`; hands files Linux programs open — e.g. Chromium
  downloads — to the editor or file manager), and `com.ImplusOS.linuxapp` (the
  generic X11 host: takes `[Title|][WxH|]/usr/bin/prog --flags` and runs any
  foreign program through `Userland/API/XSession.c`). Each has its own
  `Makefile` that pulls in `Userland/Source/AppCommon.mk` -- except
  `Translator`, a static host-Linux-ABI program with its own build (staged
  as `/Userland/Translator/x86_64.ELF` on arm64, `aarch64.ELF` on x86_64).
- **Application list**: the window manager builds the Start menu and the
  desktop icons from freedesktop.org desktop entries — `/var/System/applications`,
  `.../windowmanager/Resource/Applications`, `/usr/local/share/applications`,
  `/usr/share/applications` (what `Vendor/LinuxRuntime` staged out of the Debian
  packages), in that precedence order, resolved by desktop file ID. So a Linux
  program reaches the desktop by way of a text file, not a bespoke launcher.
  Parser: `.../windowmanager/Linux/WM_DesktopEntry.c`; full description in
  `Docs/Architecture/Userland_Specification.md` §6.
  `Resource/Apps/apps.list` is now only a fallback for an image whose entries
  are missing, and `Resource/Apps/desktop.icons` seeds the desktop grid.
- **POSIX layer**: `libc/I_libc/Source/src/posix.c` maps native syscalls to
  POSIX (open/read/write/fork/exec/socket/pthread/...). Name lookup goes through
  `getaddrinfo()`/`gethostbyname()`, backed by the DNS stub resolver in
  `libc/I_libc/Source/src/resolv.c` (UDP, TCP fallback on truncation, TTL
  cache). Applications resolve names only through `<netdb.h>`.
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
  RAM, VirtIO-Net, XHCI USB, NVMe/AHCI storage. On arm64 `run_uefi_cdrom`
  attaches the image as virtio-blk (AAVMF cannot boot an AHCI CD) plus a
  `ramfb` display; the PL011 UART at 0x09000000 is the serial port.
- Serial (COM1, 115200 baud, mapped to `-serial stdio`) is the main debug
  channel; `boot_profile_dump("boot")` prints per-phase timings.
- There is no automated test suite in-tree. `Docs/Architecture/CI_CD.md`
  describes intended GitHub Actions workflows; the `.github/` directory is not
  currently committed.
- The window manager does have host-compiled unit tests:
  `make -C Userland/Application/com.ImplusOS.windowmanager test ARCH=x86_64`
  builds `Tests/*.c` against the host libc with the file and display syscalls
  stubbed out, and runs them. Worth doing before a QEMU boot.

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
| `Userland/Application/com.ImplusOS.windowmanager/Linux/WM_DesktopEntry.c` | Desktop-entry + icon-theme reader behind the Start menu |
| `libc/I_libc/Source/src/posix.c` | POSIX layer (incl. `getaddrinfo`) |
| `Kernel/config/arch.mk` | Per-architecture compiler/linker flags |
| `Docs/Architecture/Boot_Sequence.md` | Canonical boot-phase reference |
| `Docs/Others/TODO_OS_Refactor.md` | Repo-wide refactor plan + status |
