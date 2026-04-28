# Repository Structure

```
ImplusOS/
├── BootLoader/                    UEFI boot application
│   ├── Loader.c                   EFI entry, ELF loader, BMP renderer
│   └── Resource/                  Fonts, Images (including BootLogo.bmp)
│
├── Kernel/                        Kernel source tree
│   ├── Kernel_Main.c              Kernel entry point, boot sequence
│   ├── Kernel_Main.h              BOOT_INFO structure definition
│   ├── Kernel_Main.ld             Kernel linker script (base 0x100000)
│   ├── KernelConfig.h             Compile-time configuration macros
│   │
│   ├── Common/
│   │   └── Status.h               os_status_t error codes
│   │
│   ├── Memory/
│   │   ├── Memory_Main.c/h        Bitmap PMM + kernel heap (malloc/free)
│   │   └── DMA_Memory.c/h         DMA buffer allocation
│   │
│   ├── Paging/
│   │   ├── Paging_Main.c/h        4-level page tables, user/kernel mapping
│   │   └── Paging.asm             CR3 manipulation, TLB flush
│   │
│   ├── GDT/
│   │   ├── GDT_Main.c/h           Global Descriptor Table + TSS
│   │   └── GDT.asm                LGDT, segment reload
│   │
│   ├── IDT/
│   │   ├── IDT_Main.c/h           Interrupt Descriptor Table
│   │   └── IDT.asm                LIDT, ISR stubs
│   │
│   ├── SMP/
│   │   ├── SMP_Main.c/h           Multi-core support
│   │   └── SMP_Trampoline.asm     AP startup trampoline
│   │
│   ├── Sync/
│   │   └── Spinlock.h             Spinlock with IRQ save/restore
│   │
│   ├── Timer/
│   │   └── Timer.c/h              PIT + LAPIC timer
│   │
│   ├── Platform/
│   │   ├── ACPI/
│   │   │   └── ACPI.c/h           RSDP/MADT parsing
│   │   ├── APIC/
│   │   │   ├── LAPIC.c            Local APIC driver
│   │   │   └── IOAPIC.c           I/O APIC driver
│   │   └── Interrupts/
│   │       └── Interrupts.c       APIC-based interrupt routing
│   │
│   ├── Syscall/
│   │   ├── Syscall_Init.c         MSR setup (STAR, LSTAR, SFMASK)
│   │   ├── Syscall_Entry.asm      SYSCALL/SYSRET entry stub
│   │   ├── Syscall_Dispatch.c     Syscall number → handler dispatch
│   │   ├── Syscall_File.c/h       File I/O syscall handlers
│   │   └── Syscall_Main.h         Syscall number definitions
│   │
│   ├── ProcessManager/
│   │   ├── ProcessManager.h       Process API + capability masks
│   │   └── ProcessManager_Create.c Process lifecycle, scheduling
│   │
│   ├── ELF/
│   │   └── ELF_Loader.c/h         User ELF + driver module ELF loader
│   │
│   ├── Drivers/
│   │   ├── module.mk              Driver build definitions
│   │   ├── Module/                Driver module manager core
│   │   └── DrvMain/               Main driver implementations
│   │       ├── Client/            Kernel-side driver clients (PCI, USB, etc.)
│   │       └── Server/            Driver module implementations (VirtIO-Net, etc.)
│   │
│   ├── IO/
│   │   ├── IO_Main.c/h            Port I/O + disk abstraction
│   │   └── Protocol/
│   │       ├── ATA/               ATA PIO protocol
│   │       └── USB_MassStorage/   USB Mass Storage SCSI protocol
│   │
│   ├── VFS/
│   │   └── VFS.c/h                Virtual File System layer
│   │
│   ├── IPC/
│   │   └── IPC_Main.c/h           Inter-process message passing
│   │
│   ├── WindowManager/
│   │   └── WindowManager_Kernel.c/h  WM kernel-side dispatcher
│   │
│   ├── Network/
│   │   ├── Network_Main.c/h       Network stack init + poll
│   │   ├── IPv4.c/h               IPv4 send/receive
│   │   ├── Network_Utils.h        Byte-order utilities
│   │   ├── UDP/                   UDP support
│   │   ├── TCP/                   TCP support (state machine, sockets)
│   │   ├── ICMP/                  Ping/Error reporting
│   │   ├── DHCP/                  Automatic IP configuration
│   │   └── DNS/                   Link-layer to name resolution
│   │
│   ├── Ethernet/
│   │   └── Ethernet.c/h           Ethernet frame TX/RX
│   │
│   ├── ARP/
│   │   └── ARP.c/h                ARP resolution + cache
│   │
│   ├── Boot/
│   │   └── LoadBar.c/h            Boot progress bar animation
│   │
│   ├── Debbuger/
│   │   ├── Serial/                COM1 serial output
│   │   ├── printf/                Kernel printf implementation
│   │   └── Panic/                 Kernel panic handler
│   │
│   └── Thirdparty/
│       ├── LICENSEFILE/           Licenses for third-party tools
│       └── stb/                   stb single-header libraries
│
├── Userland/                      Userland source tree
│   ├── Userland.c                 Init process (_start)
│   ├── Userland.ld                User linker script (base 0x4000000000)
│   ├── Syscalls.c/h               Raw syscall wrappers
│   ├── API/                       High-level userland API headers
│   │   ├── Process.h
│   │   ├── File.h
│   │   ├── Memory.h
│   │   ├── Input.h
│   │   ├── IPC.h
│   │   ├── Graphics.h
│   │   ├── Window.h
│   │   ├── Serial.h
│   │   ├── Network.h
│   │   ├── Socket.h               Berkeley-style sockets (TCP/UDP)
│   │   ├── XMLParser.h/c          XML utility library
│   │   ├── WM_Protocol.h          Window Manager protocol
│   │   ├── Socket.h               Socket API
│   │   └── Error.h
│   └── Application/
│       ├── SystemApps/            System services
│       │   ├── com_ImplusOS_mousemanager/
│       │   ├── com_ImplusOS_shell/
│       │   ├── com_ImplusOS_system/
│       │   └── com_ImplusOS_windowmanager/
│       └── UserApps/              User applications
│           ├── com_ImplusOS_clock/
│           ├── com_ImplusOS_editor/
│           ├── com_ImplusOS_exampleApp/
│           ├── com_ImplusOS_filemanager/
│           ├── com_ImplusOS_ImplusStore/
│           ├── com_ImplusOS_musl_test/
│           ├── com_ImplusOS_NetworkTest/
│           └── org_ffmpeg_git_ffmpeg_git/
│
├── libc/                          Minimal C library
│   ├── include/                   assert.h, math.h, stdio.h, stdlib.h, string.h,
│   │                              ctype.h, unistd.h, time.h, errno.h
│   └── src/                      Implementations
│
├── musl/                          musl standard C library
│
├── WaylandPort/                   Wayland compositor port and dependencies
│
├── Docs/                          Documentation
│   ├── Architecture/              Technical reference docs
│   ├── ForUsers/                  User guides (English + Japanese)
│   └── Images/                    Screenshots
│
├── Build/                         Build output (generated)
├── Image/                         Disk image output (generated)
├── Qemu/                          QEMU helper files
│
├── Makefile                       Top-level build system
├── Doxyfile                       Doxygen configuration
├── LICENSE                        MIT License
└── README.md                      Project overview
```
