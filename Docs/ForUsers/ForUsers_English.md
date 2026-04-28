# ImplusOS — User Guide (English)

## Quick Start

### Prerequisites

ImplusOS builds on **Linux** (Debian system or similar distributions). You need a
working x86-64 cross-compilation toolchain.

### Install Dependencies

```bash
sudo apt install -y build-essential pkg-config git make cmake
sudo apt install -y gcc-multilib g++-multilib
sudo apt install -y nasm binutils gnu-efi
sudo apt install -y gcc-x86-64-elf g++-x86-64-elf  # x86_64-elf cross-toolchain
sudo apt install -y parted dosfstools xorriso mtools util-linux
sudo apt install -y qemu-system-x86 gdb
```

### Build

```bash
make
```

This produces:
- `Build/Loader/BOOTX64.EFI` — UEFI boot application
- `Build/Kernel/Kernel_Main.ELF` — Kernel binary
- `Build/Kernel/Drivers/*.ELF` — Driver modules
- `Build/Userland/*.ELF` — Userland binaries

### Run in QEMU

**IDE boot** (virtual hard disk):
```bash
make run_ide
```

**USB boot** (virtual USB stick via xHCI):
```bash
make run_usb
```

Both commands will:
1. Build all components
2. Create a FAT32 disk image (`Image/disk.iso`)
3. Launch QEMU with OVMF UEFI firmware

### Serial Output

Boot log and debug output are sent to the serial port. In the default QEMU
configuration, this is redirected to `stdio` (your terminal).

---

## Disk Image Layout

The generated ISO contains an EFI System Partition with this structure:

```
/
├── EFI/
│   └── BOOT/
│       ├── BOOTX64.EFI        ← UEFI bootloader
│       └── Resource/          ← Resources (Images/BootLogo.bmp, Fonts)
├── Kernel/
│   ├── Kernel_Main.ELF        ← Kernel binary
│   └── Driver/
│       ├── PCI_Driver.ELF
│       ├── FAT32_Driver.ELF
│       ├── PS2_Driver.ELF
│       ├── VirtIO_Driver.ELF
│       ├── ImplusOS_Generic_Display_Driver.ELF
│       └── USB_Driver.ELF
└── Userland/
    ├── Userland.ELF            ← Init process
    ├── SystemApps/             ← System applications
    │   ├── com_ImplusOS_system/
    │   ├── com_ImplusOS_windowmanager/
    │   ├── com_ImplusOS_gui_demo/
    │   └── com_ImplusOS_mousemanager/
    └── UserApps/               ← User applications
        ├── com_ImplusOS_exampleApp/
        └── com_ImplusOS_NetworkTest/
```

---

## QEMU Configuration

### Default Settings

| Setting | IDE boot | USB boot |
|---|---|---|
| Machine | `pc` (i440FX) | `pc` (i440FX) |
| CPUs | 4 | 4 |
| RAM | 4 GiB | 15 GiB |
| USB | xHCI controller | xHCI controller |
| Network | VirtIO-Net (user mode) | VirtIO-Net (user mode) |
| UEFI firmware | OVMF 4M | OVMF 4M |
| Input | USB keyboard + mouse | USB keyboard + mouse |

### OVMF Path

The Makefile expects OVMF at:
```
/usr/share/OVMF/OVMF_CODE_4M.fd
```

If your distribution places it elsewhere, override:
```bash
make run_ide OVMF_CODE=/path/to/OVMF_CODE.fd
```

---

## Build Targets

| Target | Description |
|---|---|
| `make` / `make all` | Build all components |
| `make image` | Build + create ISO image |
| `make run_ide` | Build + image + launch QEMU (IDE mode) |
| `make run_usb` | Build + image + launch QEMU (USB mode) |
| `make clean` | Remove Build/ and Image/ directories |

---

## Debugging

### GDB Attach

Start QEMU with debugging:
```bash
qemu-system-x86_64 ... -S -s
```

Then attach GDB:
```bash
gdb Build/Kernel/Kernel_Main.ELF
(gdb) target remote :1234
(gdb) continue
```

### Serial Console

All `serial_write_*` calls and kernel `printf` output appear on the serial
port (redirected to terminal via `-serial stdio`).

---

## Troubleshooting

### Build fails: `x86_64-elf-gcc not found`

Install or build an `x86_64-elf` cross-compiler, or modify the `CC`/`CXX`/`LD`
variables in the Makefile to point to your system's cross-compiler.

### QEMU fails: `OVMF_CODE_4M.fd not found`

Install the OVMF package:
```bash
sudo apt install ovmf
```

Or download OVMF from [tianocore](https://github.com/tianocore/edk2/releases).

### Black screen after boot

- Check serial output for error messages.
- Ensure the disk image was created correctly: `ls -la Image/disk.iso`
- Try the IDE boot mode if USB boot fails.

---

## Notes

- This project assumes an **interactive Linux environment** (local terminal).
- Non-interactive environments such as CI/CD or restricted containers may not
  be able to build successfully due to `sudo` requirements for disk image
  creation.
- Physical hardware operation is not guaranteed; verified operation is
  QEMU + OVMF centric.
- **New Features**: Standard Berkeley Sockets API and C++ application support
  have been added to the userland environment.
