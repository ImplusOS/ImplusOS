# ImplusOS Kernel Architecture

## 1. Overview

ImplusOS is a hobby operating system targeting the **x86-64** architecture.
It boots via UEFI, initialises hardware through a monolithic-style kernel, and
then transitions to a userland init process that spawns system services and user
applications. The kernel is written entirely in C and x86-64 assembly (NASM).

Key design characteristics:

| Property | Value |
|---|---|
| Architecture | x86-64 (Long Mode) |
| Boot method | UEFI via `gnu-efi` |
| Kernel model | Monolithic with loadable driver modules (ELF shared objects) |
| Filesystem | FAT32 (read / write) |
| Userland ABI | `syscall` / `sysret` (AMD64) |

---

## 2. Boot Sequence

```
UEFI Firmware
  │
  ├─ EFI System Partition
  │   └─ EFI/BOOT/BOOTX64.EFI   ← BootLoader/Loader.c
  │
  ▼
BOOTX64.EFI (UEFI Application)
  │  1. Initialise GOP (Graphics Output Protocol)
  │  2. Display boot logo (BMP alpha-blend)
  │  3. Detect boot drive type (IDE / USB) via EFI Device Path
  │  4. Find partition start LBA (GPT / El Torito fallback)
  │  5. Discover ACPI RSDP from EFI Configuration Tables
  │  6. Load Kernel ELF from /Kernel/Kernel_Main.ELF
  │  7. Parse ELF64, allocate pages at physical load addresses
  │  8. Pre-load driver module ELFs into memory below 4 GiB
  │  9. Exit Boot Services (get final memory map)
  │  10. Jump to kernel_main(BOOT_INFO*)
  │
  ▼
kernel_main() — Kernel/Kernel_Main.c
  │  Phase 1: CPU Setup
  │    ├─ CLI
  │    ├─ Copy BOOT_INFO to static storage
  │    ├─ Switch to kernel stack (_stack_top from linker script)
  │    ├─ Init load bar animation
  │    ├─ Serial port init (COM1, 115200 baud)
  │    ├─ GDT init (6 segments + TSS)
  │    ├─ IDT init (256 entries)
  │    ├─ Physical memory manager init (EFI memory map → bitmap)
  │    ├─ Paging init (4-level page tables, identity + higher-half)
  │    └─ Heap allocator init (malloc / free)
  │
  │  Phase 2: Platform
  │    ├─ SMP init
  │    ├─ ACPI init (parse RSDP → RSDT/XSDT → MADT)
  │    ├─ LAPIC / I/O APIC configuration from ACPI
  │    ├─ Timer init (PIT → LAPIC timer)
  │    └─ STI
  │
  │  Phase 3: Drivers & Subsystems
  │    ├─ Driver module manager init (register pre-loaded ELFs)
  │    ├─ Boot framebuffer handoff to display subsystem
  │    ├─ Disk I/O init (ATA or USB Mass Storage)
  │    ├─ FAT32 + VFS init
  │    ├─ Display init (probe: VirtIO-GPU > Generic framebuffer)
  │    ├─ Window Manager kernel-side init
  │    ├─ PS/2 input init
  │    ├─ USB driver client init
  │    ├─ Network stack init (Ethernet → ARP → IPv4 → UDP)
  │    ├─ Syscall init (MSR: STAR, LSTAR, SFMASK)
  │    ├─ Process manager init
  │    ├─ IPC init
  │    └─ Syscall File init
  │
  │  Phase 4: Transition to Userland
  │    ├─ Register boot process from /Userland/Userland.ELF
  │    ├─ Switch CR3 to user page table
  │    ├─ Set up iretq frame (user CS/DS/SS/RSP/RIP)
  │    └─ iretq → Ring 3
```

---

## 3. Memory Layout

### 3.1 Physical Memory

| Region | Description |
|---|---|
| 0x00000000 – 0x000FFFFF | Low memory (reserved) |
| 0x00100000 (`_kernel_start`) | Kernel .text start |
| `_kernel_end` + 256 KiB stack | End of kernel BSS + 256 KiB kernel stack |
| Free pages | Managed by bitmap-based PMM |
| Below 4 GiB | Driver module ELF images (pre-loaded by bootloader) |

### 3.2 Virtual Memory (4-Level Paging)

```
0x0000_0000_0000_0000 ┐
            ...        │  Identity-mapped low memory (kernel use)
0x0000_003F_FFFF_FFFF ┘

0x0000_0040_0000_0000 ┐  USER_CODE_BASE
            ...        │  User code (NX protected), heap, stack
0x0000_0048_0000_0000 ┘  USER_STACK_TOP

Kernel higher-half region   (MMIO, device memory)

### 3.3 Security Features (NX Bit)

ImplusOS utilizes the **No-Execute (NX)** bit in the 4-level page tables to prevent code execution from data-only memory regions (stack, heap, DMA buffers). The NX bit is globally enabled in `IA32_EFER` and enforced via `init_paging()`.
```

### 3.3 User Process Address Space

| Range | Use |
|---|---|
| `0x4000000000` – `0x4100000000` | Code segment (ELF load) |
| `0x4100000000` – `0x47E0000000` | User heap (grows up via `user_alloc` / `mmap`) |
| `0x47E0000000` – `0x4800000000` | User stack (32 MiB, grows down) |

### 3.4 Kernel Heap

The kernel provides `malloc()`, `calloc()`, `realloc()`, `free()` backed by a
page-based allocator. Sensitive allocations use `malloc_sensitive()` /
`free_sensitive()` which zero memory on free.

DMA memory is separately managed via `dma_alloc()` / `dma_free()`, returning
physically-contiguous, identity-mapped buffers with physical addresses provided
out-of-band.

---

## 4. CPU Management

### 4.1 GDT

Six segment descriptors plus one TSS entry:

| Selector | Segment |
|---|---|
| `0x08` | Kernel Code (64-bit) |
| `0x10` | Kernel Data |
| `0x18` | User Compat Code (32-bit, unused) |
| `0x20` | User Data |
| `0x28` | User Code (64-bit) |
| `0x30` | TSS |

### 4.2 IDT

256 entries. Exceptions 0–31 are set to dedicated handlers. IRQs are routed via
I/O APIC (or PIC fallback). The syscall entry is handled through the `SYSCALL` /
`SYSRET` mechanism (MSR-based), not an IDT gate.

### 4.3 SMP

- Up to `OS_CONFIG_SMP_MAX_CPUS` (default 4) cores
- AP boot via trampoline page (`SMP_Trampoline.asm`)
- TLB shootdown via IPI vector `0xFE`
- Per-CPU PID tracking

---

## 5. Driver Module System

ImplusOS uses a **loadable driver module** architecture. Drivers are compiled as
**position-independent ELF shared objects** (`ET_DYN`) and pre-loaded into
memory by the UEFI bootloader before `ExitBootServices()`.

### 5.1 Module Lifecycle

```
Bootloader                     Kernel
    │                            │
    ├── Load ELF files ──────────┤
    │   from ESP:/Kernel/Driver/ │
    │                            │
    └── Store in BOOT_INFO ──────┤
         .LoadedFiles[]          │
                                 │
         driver_module_manager_init()
              │
              ├── Copy each ELF blob into kernel heap
              ├── Register by module ID
              │
              └── On demand: driver_module_manager_load()
                      │
                      ├── ELF relocations (R_X86_64_RELATIVE)
                      ├── Call entry point: driver_module_init()
                      └── Returns driver vtable pointer
```

### 5.2 Module IDs

| ID | Name | Module |
|---|---|---|
| 1 | `DRIVER_MODULE_ID_PCI` | PCI bus scanner |
| 2 | `DRIVER_MODULE_ID_FAT32` | FAT32 filesystem |
| 3 | `DRIVER_MODULE_ID_PS2` | PS/2 keyboard/mouse input |
| 4 | `DRIVER_MODULE_ID_DISPLAY_VIRTIO` | VirtIO-GPU display |
| 5 | `DRIVER_MODULE_ID_DISPLAY_IMPLUS_DISPLAY_GENERIC_DRIVER` | Generic framebuffer |
| 6 | `DRIVER_MODULE_ID_USB` | USB host controller (OHCI/UHCI/EHCI/XHCI) |

### 5.3 Kernel API Surface

Each driver module receives a `driver_kernel_api_t` pointer from the kernel,
providing access to:

- **Timer**: `timer_msleep`, `timer_hz`, `timer_ticks`
- **Memory**: `malloc`, `free`, `dma_alloc`, `dma_free`, `virt_to_phys`
- **Memops**: `memset`, `memcpy`
- **Port I/O**: `inb`, `outb`, `inl`, `outl`
- **Disk**: `disk_read`, `disk_write`
- **PCI**: `pci_read_config`, `pci_write_config`
- **MMIO**: `map_mmio_virt`
- **Debug**: `serial_write_char`, `serial_write_string`, `serial_write_uint32`

### 5.4 Build Flags

Driver modules are built with:

```
CFLAGS:  -fPIC -DIMPLUS_DRIVER_MODULE -DKERNEL
LDFLAGS: -shared -Bsymbolic -e driver_module_init -z max-page-size=4096
```

---

## 6. Filesystem & VFS

### 6.1 VFS Layer

The VFS (`Kernel/Core/vfs/`) provides a unified interface with mount-point prefix
matching. Currently the only backend is FAT32, mounted at `/`.

Key operations:

| Function | Description |
|---|---|
| `vfs_find_file` | Lookup file by path |
| `vfs_read_file` / `vfs_write_file` | Whole-file I/O |
| `vfs_read_at` / `vfs_write_at` | Offset-based I/O |
| `vfs_truncate` | Resize file |
| `vfs_creat` / `vfs_mkdir` | Create files/directories |
| `vfs_opendir` / `vfs_readdir` / `vfs_closedir` | Directory enumeration |
| `vfs_unlink` | Delete file |

Case sensitivity is configurable via `vfs_set_case_sensitive()`.

### 6.2 FAT32 Driver

The FAT32 driver implements:

- BPB parsing, FAT chain traversal, cluster caching
- Long file name (LFN) entry support
- Read/write at arbitrary offsets
- Directory creation, file creation, deletion
- Configurable case-sensitive/insensitive lookup

### 6.3 Disk I/O

The `IO/IO_Main` module provides a protocol-agnostic disk interface:

| Protocol | Backend |
|---|---|
| `IO_PROTOCOL_TYPE_ATA` | ATA PIO (IDE) |
| `IO_PROTOCOL_TYPE_USB_MASS_STORAGE` | USB Mass Storage via SCSI |

The active protocol is selected automatically based on `BOOT_INFO.BootDriveType`.

---

## 7. Process Manager

- Maximum `OS_CONFIG_PROCESS_MAX_COUNT` processes (default 32, max 256)
- Each process has its own CR3 (page table), user stack, heap, and capability mask
- ELF loading for user processes via the VFS
- Round-robin scheduling with timer-based preemption (`process_on_timer_tick`)
- Cooperative yield via `SYSCALL_PROCESS_YIELD`

### 7.1 Capabilities

```c
PROCESS_CAP_SERIAL   (1 << 0)  // Serial output
PROCESS_CAP_PROCESS  (1 << 1)  // Process management
PROCESS_CAP_FILE     (1 << 2)  // File I/O
PROCESS_CAP_MEMORY   (1 << 3)  // Memory allocation
PROCESS_CAP_INPUT    (1 << 4)  // Input devices
PROCESS_CAP_SIGNAL   (1 << 5)  // Signal handling
PROCESS_CAP_IPC      (1 << 6)  // Inter-process communication
PROCESS_CAP_NETWORK  (1 << 7)  // Network access
```

All capabilities are granted by default (`PROCESS_CAP_DEFAULT_MASK`).

---

## 8. Syscall Interface

System calls use the AMD64 `SYSCALL` / `SYSRET` mechanism. The entry point is
set via `MSR_LSTAR` to `syscall_entry` (assembly stub in `Syscall_Entry.asm`),
which saves registers and calls `syscall_dispatch()`.

### 8.1 Syscall Numbers

| Number | Name | Category |
|---|---|---|
| 1–5 | `SERIAL_PUTCHAR`, `PUTS`, `WRITE_U64/U32/U16` | Debug serial |
| 6–9 | `PROCESS_CREATE`, `YIELD`, `EXIT`, `THREAD_CREATE` | Process |
| 23–26, 34 | `FILE_OPEN`, `READ`, `WRITE`, `CLOSE`, `SEEK` | File I/O |
| 37–42 | `MKDIR`, `OPENDIR`, `READDIR`, `CLOSEDIR`, `UNLINK`, `CREAT` | Directory ops |
| 27–31 | `USER_MALLOC`, `FREE`, `MEMCPY`, `MEMCMP`, `MEMSET` | Memory |
| 32–33 | `INPUT_READ_KEYBOARD`, `INPUT_READ_MOUSE` | Input |
| 36 | `PROCESS_SPAWN_ELF` | Process (ELF loading) |
| 43 | `USER_MMAP` | Memory mapping |
| 44 | `PROCESS_SIGNAL` | Signals |
| 45–46 | `IPC_SEND_MESSAGE`, `IPC_RECEIVE_MESSAGE` | IPC |
| 47 | `PROCESS_GET_PID` | Process info |
| 48–54 | Display & Window syscalls | Graphics |
| 60 | `GET_FAT32_FILE_T` | Filesystem info |
| 100–107 | `UDP_SEND`, `TCP_CONNECT`, `LISTEN`, `ACCEPT`, etc. | Network (UDP/TCP) |
| 110–112 | `WAITPID`, `GETPPID`, `EXIT_STATUS` | Process Info |
| 113–120 | `SLEEP`, `NANOSLEEP`, `GET_UPTIME_MS` | Time Utilities |
| 114–118 | `STAT`, `PIPE`, `DUP`, `DUP2`, `GETCWD` | More File I/O |
| 121–122 | `GET_PROC_COUNT`, `GET_PROC_INFO` | System Monitor |
| 130–137 | `SOCKET_CREATE`, `BIND`, `RECV`, `SEND`, etc. | Berkeley Sockets |

---

## 9. IPC (Inter-Process Communication)

Message-passing IPC with per-process queues:

- **Message size**: Up to `IPC_MESSAGE_MAX_SIZE` (256 bytes)
- **Queue depth**: Up to `IPC_MAX_MESSAGES_PER_PROCESS` (16 messages)
- **Ring buffer** implementation (head/tail/count)
- Identified by sender PID

Used extensively by the Window Manager for keyboard/mouse event delivery and
window management commands.

---

## 10. Window Manager

The Window Manager has a **kernel-side dispatcher** (`WindowManager_Kernel`) and
a **userland service process** (`com_ImplusOS_windowmanager`).

### 10.1 Kernel Side

- `wm_kernel_init()` — initialise kernel WM state
- `wm_kernel_register_service(pid)` — register the WM service process
- Routes input events and display commands between processes via IPC

### 10.2 WM Commands

| Command | ID | Description |
|---|---|---|
| `WM_CREATE_WINDOW` | 1 | Create a new window |
| `WM_DESTROY_WINDOW` | 2 | Destroy a window |
| `WM_SET_WINDOW_RECT` | 3 | Move/resize |
| `WM_SHOW_WINDOW` / `WM_HIDE_WINDOW` | 4, 5 | Visibility |
| `WM_RAISE_WINDOW` / `WM_LOWER_WINDOW` | 6, 7 | Z-ordering |
| `WM_DRAW_PIXEL` / `WM_DRAW_RECT` | 10, 11 | Drawing primitives |
| `WM_BLIT_BUFFER` | 14 | Buffer blit |
| `WM_UPDATE_COMPLETE` | 13 | Present frame |
| `WM_KEYBOARD_EVENT` / `WM_MOUSE_EVENT` | 20, 21 | Input events |

---

## 11. Display Subsystem

### 11.1 Driver Interface

All display drivers implement the `display_driver_t` vtable:

```c
const char *name;
bool (*probe)(void);          // Can this driver work?
bool (*init)(void);           // Initialise hardware
bool (*is_ready)(void);
uint32_t (*width)(void);
uint32_t (*height)(void);
void (*draw_pixel)(x, y, color);
void (*fill_rect)(x, y, w, h, color);
void (*present)(void);        // Flip / flush
bool (*set_framebuffer)(const display_boot_framebuffer_t *);
```

### 11.2 Available Drivers

| Driver | Description |
|---|---|
| **VirtIO-GPU** | Paravirtualised GPU for QEMU/KVM |
| **Generic Framebuffer** | Direct linear framebuffer with double-buffering |

Driver selection uses probe-based priority via `driver_select_pick_display_driver()`.

---

## 12. Network Stack

```
Application (UDP send syscall)
       │
   ┌───▼───┐
   │  UDP   │  Kernel/Network/UDP/
   ├────────┤
   │  IPv4  │  Kernel/Network/IPv4.c
   ├────────┤
   │  ARP   │  Kernel/ARP/ARP.c
   ├────────┤
   │Ethernet│  Kernel/Ethernet/Ethernet.c
   ├────────┤
   │VirtIO- │
   │  Net   │  Kernel/Drivers/Server/NIC/VirtIONet/
   └────────┘
```

- **Ethernet**: Frame TX/RX, handler registration by EtherType
- **ARP**: Address resolution with cache
- **IPv4**: Packet send/receive, protocol handler registration
- **UDP**: Datagram send/receive
- **TCP**: Full state machine (SYN/ACK, retransmission, window scaling)
- **ICMP**: Ping (Echo Request/Reply), TTL exceeded, Port unreachable
- **DNS**: Name resolution support
- **DHCP**: Automatic address assignment on link-up
- **NIC**: VirtIO-Net paravirtualised driver

Network configuration defaults (compile-time):
- IP: `10.0.2.15` (`OS_CONFIG_NET_IPV4_ADDR`)
- Mask: `255.255.255.0`
- Gateway: `10.0.2.2`

---

## 13. Synchronisation

The kernel uses **spinlocks** with IRQ save/restore:

```c
spinlock_t lock;
spinlock_init(&lock);

uint64_t flags = irq_save_disable();
spinlock_lock(&lock);
// critical section
spinlock_unlock(&lock);
irq_restore(flags);
```

Spinlocks use `__atomic_exchange_n` (acquire) and `__atomic_store_n` (release)
with a TTAS (test-and-test-and-set) loop and `PAUSE` hint for efficiency.

A full sequential-consistency barrier is available via `memory_barrier_full()`.

---

## 14. Debug & Diagnostic

- **Serial**: COM1 output at 115200 baud (`Debug/serial/`)
- **printf**: Kernel-space printf implementation (`Debug/printf/`)
- **Panic**: `panic(const char *msg)` — halts with message
- **Load Bar**: Boot progress animation on framebuffer (`Boot/LoadBar`)
- **Memory dump**: `memory_dump_virtual()` / `memory_dump_physical()`

---

## 15. Error Handling

Kernel subsystems return `os_status_t` (signed 64-bit):

| Status | Value | Description |
|---|---|---|
| `OS_STATUS_OK` | 0 | Success |
| `OS_STATUS_NOT_FOUND` | -2 | Resource not found |
| `OS_STATUS_IO_ERROR` | -5 | I/O error |
| `OS_STATUS_ACCESS_DENIED` | -13 | Permission denied |
| `OS_STATUS_FAULT` | -14 | Memory fault |
| `OS_STATUS_INVALID_ARG` | -22 | Invalid argument |
| `OS_STATUS_LIMIT_REACHED` | -24 | Resource limit |
| `OS_STATUS_NOT_SUPPORTED` | -95 | Not supported |
| `OS_STATUS_INTERNAL` | -255 | Internal error |

Userland sees errno values via `os_status_to_errno()`.

---

## 16. ELF Loader

Two loading modes:

1. **User process ELF** (`elf_loader_load_from_path`):
   - Reads from VFS, loads `PT_LOAD` segments into user address space
   - Validates against `elf_load_policy_t` (max file size, vaddr range)

2. **Driver module ELF** (`elf_loader_load_module_from_memory`):
   - Loads from in-memory buffer (pre-loaded by bootloader)
   - Processes `R_X86_64_RELATIVE` relocations for position-independent code
   - Validates against `elf_module_load_policy_t`

---

## 17. Kernel Configuration

Compile-time configuration via `Kernel/KernelConfig.h`:

| Macro | Default | Description |
|---|---|---|
| `OS_CONFIG_PROCESS_MAX_COUNT` | 128 | Max concurrent processes |
| `OS_CONFIG_FILE_MAX_FD` | 32 | Max file descriptors |
| `OS_CONFIG_FILE_MAX_DIR_HANDLE` | 32 | Max directory handles |
| `OS_CONFIG_SMP_MAX_CPUS` | 4 | Max CPU cores |
| `OS_CONFIG_SMP_ENABLED` | 1 | SMP support |
| `OS_CONFIG_LOG_FILE_MAX_BYTES` | 512 KiB | Log file size limit |
| `OS_CONFIG_ALLOW_DISKLESS_BOOT` | 0 | Boot without filesystem |
| `OS_CONFIG_SIGNAL_HANDLER_MAX_PER_PROCESS` | 32 | Signal handlers/process |
| `OS_CONFIG_PENDING_SIGNAL_MAX_PER_PROCESS` | 64 | Pending signals/process |
| `OS_CONFIG_NET_IPV4_ADDR` | `10.0.2.15` | Network IP |
| `OS_CONFIG_NET_IPV4_MASK` | `255.255.255.0` | Subnet mask |
| `OS_CONFIG_NET_IPV4_GATEWAY` | `10.0.2.2` | Default gateway |

All values include compile-time range validation.
