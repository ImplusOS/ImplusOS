# Kernel Configuration Guide

This document describes the compile-time and runtime configuration options
available in ImplusOS. All kernel configuration macros are defined in
`Kernel/KernelConfig.h`.

---

## 1. How Configuration Works

Configuration values are set via C preprocessor macros. To override a default,
pass `-D<MACRO>=<value>` to the compiler, or edit `KernelConfig.h` directly.

Each configuration macro has:
- A default value
- Minimum and maximum bounds (enforced at compile time with `#error`)
- Cross-validation against related macros

---

## 2. Process Configuration

### `OS_CONFIG_PROCESS_MAX_COUNT`

Maximum number of concurrent user processes.

| Property | Value |
|---|---|
| Default | 128 |
| Min | 1 |
| Max | 256 |

Higher values increase static memory usage for process control blocks.

### `OS_CONFIG_SIGNAL_HANDLER_MAX_PER_PROCESS`

Maximum number of signal handlers a single process can register.

| Property | Value |
|---|---|
| Default | 32 |

### `OS_CONFIG_PENDING_SIGNAL_MAX_PER_PROCESS`

Maximum number of pending (queued) signals per process.

| Property | Value |
|---|---|
| Default | 64 |

---

## 3. File System Configuration

### `OS_CONFIG_FILE_MAX_FD`

Maximum number of open file descriptors (system-wide).

| Property | Value |
|---|---|
| Default | 32 |
| Min | 4 |
| Max | 256 |

### `OS_CONFIG_FILE_MAX_DIR_HANDLE`

Maximum number of open directory handles (system-wide).
Must be ≤ `OS_CONFIG_FILE_MAX_FD`.

| Property | Value |
|---|---|
| Default | 32 |
| Min | 4 |
| Max | 256 |

---

## 4. SMP Configuration

### `OS_CONFIG_SMP_MAX_CPUS`

Maximum number of CPU cores the kernel will attempt to initialise.

| Property | Value |
|---|---|
| Default | 4 |

### `OS_CONFIG_SMP_ENABLED`

Set to `0` to disable SMP entirely (single-core operation).

| Property | Value |
|---|---|
| Default | 1 (enabled) |

---

## 5. Boot Configuration

### `OS_CONFIG_ALLOW_DISKLESS_BOOT`

When set to `1`, the kernel will continue booting even if the filesystem
fails to initialise. The system will halt at an idle loop since no userland
can be loaded.

| Property | Value |
|---|---|
| Default | 0 (disabled) |

This is primarily useful for debugging kernel initialisation without a
valid disk image.

---

## 6. Logging Configuration

### `OS_CONFIG_LOG_FILE_MAX_BYTES`

Maximum size of the kernel log file before truncation.

| Property | Value |
|---|---|
| Default | 524288 (512 KiB) |

---

## 7. Network Configuration

Static network configuration. These values are compiled into the kernel.

### `OS_CONFIG_NET_IPV4_ADDR`

Local IPv4 address in network byte order (big-endian u32).

| Property | Value |
|---|---|
| Default | `0x0A00020F` → `10.0.2.15` |

### `OS_CONFIG_NET_IPV4_MASK`

Subnet mask.

| Property | Value |
|---|---|
| Default | `0xFFFFFF00` → `255.255.255.0` |

### `OS_CONFIG_NET_IPV4_GATEWAY`

Default gateway IPv4 address.

| Property | Value |
|---|---|
| Default | `0x0A000202` → `10.0.2.2` |

> **Note:** These defaults match QEMU's user-mode networking (`-netdev user`),
> where the host is `10.0.2.2` and the guest is typically `10.0.2.15`.

---

## 8. GDT Segment Selectors

These are fixed and should not be changed without updating the assembly stubs:

| Macro | Value | Segment |
|---|---|---|
| `GDT_KERNEL_CODE` | `0x08` | Kernel code segment |
| `GDT_KERNEL_DATA` | `0x10` | Kernel data segment |
| `GDT_USER_COMPAT_CODE` | `0x18` | User compat code (32-bit) |
| `GDT_USER_DATA` | `0x20` | User data segment |
| `GDT_USER_CODE` | `0x28` | User code segment (64-bit) |
| `GDT_TSS` | `0x30` | Task State Segment |

---

## 9. Overriding Configuration

### Method 1: Compiler flags

Add to `KERNEL_CFLAGS` in the Makefile:

```makefile
KERNEL_CFLAGS += -DOS_CONFIG_PROCESS_MAX_COUNT=64
```

### Method 2: Edit KernelConfig.h

```c
// Before the #ifndef guard:
#define OS_CONFIG_PROCESS_MAX_COUNT 64
```

### Validation

The build will fail with `#error` if:
- A value is outside its valid range
- `FILE_MAX_DIR_HANDLE_CONFIG > FILE_MAX_FD_CONFIG`
