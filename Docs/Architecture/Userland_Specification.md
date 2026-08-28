# Userland Specification — ImplusOS

## 1. Overview

The ImplusOS userland runs in Ring 3 (x86_64) / EL0 (arm64) with a separate
address space per process. All kernel services are accessed via architecture-specific
trap instructions (x86_64: `SYSCALL`; arm64: `SVC`). The init process
(`Userland.ELF`) spawns system and user applications.

## 2. Init Process (`Userland/Userland.c`)

The init process is the first user-space process loaded by the kernel.

### Boot Sequence

1. Clear the display (gradient fill)
2. Load system font
3. Display welcome message and boot count
4. Spawn system apps:
   - `com.ImplusOS.windowmanager` — Window manager
   - `com.ImplusOS.version` — Version display
5. Enter idle loop (`process_yield()` forever)

Each spawn is followed by yield loops to give the spawned process time to initialize.

### Entry Point

User-space binaries use `_start()` as their entry point (no `main()`).
The linker script (`Userland.ld`) sets up the correct entry address.

## 3. Syscall API (`Userland/API/`)

The userland exposes typed C wrapper functions for each syscall category:

### API Headers

| Header | Category | Key Functions |
|---|---|---|
| `Serial.h` | Serial I/O | `serial_putchar()`, `serial_puts()` |
| `Process.h` | Process | `process_spawn()`, `process_yield()`, `process_exit()`, `process_get_current_pid()`, `process_waitpid()`, `process_getppid()`, `sleep_ms()`, `get_uptime_ms()` |
| `File.h` | File I/O | `file_open()`, `file_read()`, `file_write()`, `file_close()`, `file_seek()`, `file_mkdir()`, `file_opendir()`, `file_readdir()` |
| `Graphics.h` | Display | `get_display_width()`, `get_display_height()`, `draw_pixel()`, `draw_fill_rect()`, `draw_present()`, `get_framebuffer()` |
| `Input.h` | Input | `input_read_keyboard()`, `input_read_mouse()` |
| `Memory.h` | Memory | `user_malloc()`, `user_free()`, `user_memcpy()`, `user_memset()`, `user_mmap()` |
| `IPC.h` | IPC | `ipc_send()`, `ipc_receive()` |
| `Window.h` | Window Mgr | `window_register_service()`, `window_get_wm_pid()` |
| `Network.h` | Networking | `udp_send()`, `tcp_connect()`, `tcp_send()`, `tcp_recv()`, `tcp_close()` |
| `Socket.h` | Sockets | `socket_create()`, `socket_connect()`, `socket_bind()`, `socket_listen()`, `socket_accept()`, `socket_send()`, `socket_recv()`, `socket_get_info()`, `socket_set_option()`, `socket_get_option()`, `socket_shutdown()` |
| `Time.h` | Time | `get_rtc_time()`, `get_uptime_ms()`, `nanosleep()` |
| `Audio.h` | Audio | `audio_open()`, `audio_get_info()`, `audio_write()`, `audio_drain()`, `audio_close()` |
| `SystemInfo.h` | System Info | `get_cpu_info()`, `get_memory_info()`, `get_disk_info()`, `get_device_info()`, `get_graphics_info()`, `get_arch_info()`, `get_system_info()` |
| `KVM.h` | Virtualization | `kvm_open()`, `kvm_ioctl()`, `kvm_close()`, `kvm_mmap()` |
| `Error.h` | Errors | `os_errno` definitions |
| `WM_Protocol.h` | WM Protocol | Window manager message definitions |

### Syscall Mechanism (x86_64)

All wrappers call through `Userland/Syscalls.c` which dispatches to the
architecture-specific implementation in `libc/I_libc/src/sys/$(ARCH)/hal_syscall.c`
(x86_64: `syscall` instruction; arm64: `SVC #0`). The x86_64 implementation:

```c
static inline uint64_t syscall6(uint64_t num, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4, uint64_t a5) {
    uint64_t ret;
    __asm__ volatile(
        "mov %[a4], %%r10\n\t"
        "mov %[a5], %%r8\n\t"
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3),
          [a4]"r"(a4), [a5]"r"(a5)
        : "rcx", "r11", "r10", "r8", "memory"
    );
    return ret;
}
```

The arm64 variant uses `SVC #0` with the syscall number in register `X8` and
arguments in `X0`–`X5`. The arch-specific implementation lives in
`libc/I_libc/src/sys/$(ARCH)/hal_syscall.c`.

## 4. POSIX Compatibility Layer (`Userland/POSIX/`)

A comprehensive POSIX API mapping layer enabling portable C programs.

### Supported APIs

| Module | Functions | Notes |
|---|---|---|
| **File I/O** | `open`, `creat`, `read`, `write`, `close`, `lseek`, `pipe`, `dup`, `dup2`, `stat`, `fstat`, `mkdir`, `unlink`, `opendir`, `readdir`, `closedir` | O_CREAT, O_TRUNC, O_APPEND, O_RDWR, O_NONBLOCK |
| **Process** | `getpid`, `getppid`, `fork`, `execve`, `execv`, `execvp`, `waitpid`, `wait`, `kill`, `_exit` | `fork()` spawns fresh process (no memory sharing) |
| **Signal** | `signal`, `sigaction`, `sigprocmask`, `sigpending`, `raise`, `sigemptyset`, `sigfillset`, `sigaddset`, `sigdelset` | SA_RESETHAND, SA_NODEFER, full signal numbers 1–31 |
| **Threads** | `pthread_create`, `pthread_join`, `pthread_detach`, `pthread_self`, `pthread_cancel`, `pthread_once`, `pthread_mutex_*`, `pthread_cond_*`, `pthread_key_*` | Mutex types: NORMAL, ERRORCHECK, RECURSIVE. 64 TLS keys × 128 threads |
| **Networking** | `socket`, `bind`, `connect`, `listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `shutdown`, `setsockopt`, `getsockopt`, `htons/ntohs/htonl/ntohl`, `inet_aton/inet_addr/inet_ntoa` | AF_INET only, SOCK_STREAM + SOCK_DGRAM |
| **Time** | `clock_gettime`, `clock_getres`, `nanosleep`, `gettimeofday`, `time`, `gmtime_r`, `localtime_r`, `mktime`, `difftime` | CLOCK_MONOTONIC, CLOCK_REALTIME |
| **Memory** | `mmap`, `munmap`, `mprotect` | munmap/mprotect are no-ops |
| **I/O Mux** | `fcntl`, `ioctl`, `select`, `poll` | select/poll: sleep-based stub |

### errno Mapping

| Kernel Status | Value | POSIX errno |
|---|---|---|
| `OS_STATUS_NOT_FOUND` | -2 | `ENOENT` (2) |
| `OS_STATUS_IO_ERROR` | -5 | `EIO` (5) |
| `OS_STATUS_ACCESS_DENIED` | -13 | `EACCES` (13) |
| `OS_STATUS_FAULT` | -14 | `EFAULT` (14) |
| `OS_STATUS_INVALID_ARG` | -22 | `EINVAL` (22) |
| `OS_STATUS_LIMIT_REACHED` | -24 | `EMFILE` (24) |
| `OS_STATUS_NOT_SUPPORTED` | -95 | `ENOTSUP` (95) |
| `OS_STATUS_INTERNAL` | -255 | `EIO` (5) |

### Limitations

1. `fork()` spawns a fresh process — no copy-on-write or shared memory
2. `mmap()` is anonymous-only natively; file-backed mmap copies data
3. `munmap()` / `mprotect()` are no-ops
4. `select()` / `poll()` sleep-based stubs (all open FDs reported ready)
5. `kill()` supports self-signal only; cross-process returns ENOSYS
6. IPv4 only (no IPv6)

## 5. Application Structure

### Directory Convention

```
Userland/Application/
├── SystemApps/
│   ├── com.ImplusOS.shell/
│   │   ├── Makefile
│   │   └── Shell.c
│   ├── com.ImplusOS.version/
│   │   ├── Makefile
│   │   └── main.c
│   └── com.ImplusOS.windowmanager/
│       ├── Makefile
│       ├── Resource/
│       │   ├── Background.png
│       │   └── Fonts/NotoSansJP-Regular.ttf
│       ├── WindowManager.c
│       └── WindowManager.h
└── UserApps/
    ├── com.ImplusOS.editor/
    ├── com.ImplusOS.exampleApp/
    ├── com.ImplusOS.filemanager/
    ├── com.ImplusOS.ImplusStore/
    ├── com.ImplusOS.NetworkTest/
    ├── com.ImplusOS.procman/
    ├── com.ImplusOS.settings/
    └── com.ImplusOS.vm/
```

### Naming Convention

Applications follow reverse-domain naming: `com.ImplusOS.<appname>`

### Build System

Each application has its own `Makefile` that includes `AppCommon.mk`:

```makefile
include ../../AppCommon.mk
```

`AppCommon.mk` provides common object files (libc, syscalls, XMLParser, DNS).

### Application ELF Loading

1. Application ELF files are placed on the boot filesystem under `Userland/` or `Userland/`
2. The init process spawns them via `process_spawn(path)` syscall
3. The kernel loads the ELF, creates a new process with separate address space
4. Application entry point: `_start()` (not `main()`)

## 6. System Applications

### Window Manager (`com.ImplusOS.windowmanager`)

- Registers as the WM service via `window_register_service()` syscall
- Manages window stacking, focus, and input routing
- Renders desktop background (PNG), window decorations, taskbar
- Forwards keyboard/mouse events to focused window via IPC
- Uses direct framebuffer access for rendering

### Shell (`com.ImplusOS.shell`)

- Terminal emulator with command input
- Communicates with WM for window and input

### Version (`com.ImplusOS.version`)

- Displays OS version information

## 7. User Applications

| Application | Description |
|---|---|---|
| `com.ImplusOS.editor` | Text editor |
| `com.ImplusOS.exampleApp` | Demo app (XML layout, resource loading) |
| `com.ImplusOS.filemanager` | File manager (directory listing, file ops) |
| `com.ImplusOS.ImplusStore` | App store prototype |
| `com.ImplusOS.NetworkTest` | Network testing tool |
| `com.ImplusOS.procman` | Process manager / system monitor |
| `com.ImplusOS.settings` | System settings application |
| `com.ImplusOS.vm` | Virtual machine application (uses KVM syscalls) |

## 8. Utility Libraries

### XML Parser (`Userland/API/XMLParser.c`)

Simple XML parser for UI layout files. Used by applications that load `layout.xml` resources.

### DNS Resolver (`Userland/Service/com.ImplusOS.netstack/DNS/DNS.c`)

Userland DNS resolver that performs DNS queries over UDP. Built as the
hot-loadable `com.ImplusOS.netstack` service.

## 9. libc (`libc/I_libc/`)

Minimal freestanding C library providing:

| Header | Functions |
|---|---|
| `string.h` | `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strchr`, `strstr` |
| `stdlib.h` | `atoi`, `itoa`, `abs`, `strtol`, `strtoul` |
| `stdio.h` | `printf`, `sprintf`, `snprintf`, `putchar`, `puts` |
| `math.h` | Basic math functions |
| `errno.h` | `errno` variable |
| `assert.h` | `assert()` macro |

Additional POSIX-compatible headers:
- `unistd.h`, `fcntl.h`, `signal.h`, `time.h`, `pthread.h`
- `sys/socket.h`, `sys/stat.h`, `sys/mman.h`, `sys/types.h`, `sys/wait.h`
- `netinet/in.h`, `arpa/inet.h`
- `dirent.h`, `poll.h`

## 10. Linker Script (`Userland/Userland.ld`)

The userland linker script is shared across architectures and sets:
- Entry point: `_start`
- Code base: `0x4000000000`
- Text, rodata, data, bss sections
- No standard library startup code
