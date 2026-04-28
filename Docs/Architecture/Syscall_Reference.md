# Syscall Reference

This document lists all system calls available to ImplusOS userland applications.
System calls use the AMD64 `SYSCALL` instruction with the following register
convention:

| Register | Purpose |
|---|---|
| `RAX` | Syscall number |
| `RDI` | Argument 1 |
| `RSI` | Argument 2 |
| `RDX` | Argument 3 |
| `R10` | Argument 4 |
| `R8` | Argument 5 |
| `RAX` (return) | Result / error code |

Defined in `Kernel/Syscall/Syscall_Main.h`.

---

## Serial Debug

| # | Name | Arguments | Return |
|---|---|---|---|
| 1 | `SERIAL_PUTCHAR` | `char c` | 0 |
| 2 | `SERIAL_PUTS` | `const char *str` | 0 |
| 3 | `SERIAL_WRITE_U64` | `uint64_t value` | 0 |
| 4 | `SERIAL_WRITE_U32` | `uint32_t value` | 0 |
| 5 | `SERIAL_WRITE_U16` | `uint16_t value` | 0 |

---

## Process Management

| # | Name | Arguments | Return |
|---|---|---|---|
| 6 | `PROCESS_CREATE` | `uint64_t entry` | PID or error |
| 7 | `PROCESS_YIELD` | — | 0 |
| 8 | `PROCESS_EXIT` | — | — (does not return) |
| 9 | `THREAD_CREATE` | `uint64_t entry` | TID or error |
| 36 | `PROCESS_SPAWN_ELF` | `const char *path` | PID or error |
| 44 | `PROCESS_SIGNAL` | `int32_t signum, uint64_t handler` | previous handler |
| 47 | `PROCESS_GET_PID` | — | current PID |
| 110 | `PROCESS_WAITPID` | `int32_t pid, int *status, int options` | pid or error |
| 111 | `PROCESS_GETPPID` | — | parent PID |
| 112 | `PROCESS_EXIT_STATUS` | `int32_t pid` | exit status |
| 121 | `GET_PROC_COUNT` | — | count |
| 122 | `GET_PROC_INFO` | `int32_t index, void *out` | 0 or error |

---

## File I/O

| # | Name | Arguments | Return |
|---|---|---|---|
| 23 | `FILE_OPEN` | `const char *path, uint32_t flags` | fd or error |
| 24 | `FILE_READ` | `int32_t fd, void *buf, uint32_t size` | bytes read or error |
| 25 | `FILE_WRITE` | `int32_t fd, const void *buf, uint32_t size` | bytes written or error |
| 26 | `FILE_CLOSE` | `int32_t fd` | 0 or error |
| 34 | `FILE_SEEK` | `int32_t fd, int32_t offset, int32_t whence` | new position or error |
| 42 | `FILE_CREAT` | `const char *path` | 0 or error |
| 60 | `GET_FAT32_FILE_T` | `int32_t fd, void *out` | 0 or error |
| 114 | `FILE_STAT` | `const char *path, void *statbuf` | 0 or error |
| 115 | `FILE_PIPE` | `int32_t fds[2]` | 0 or error |
| 116 | `FILE_DUP` | `int32_t oldfd` | newfd or error |
| 117 | `FILE_DUP2` | `int32_t oldfd, int32_t newfd` | newfd or error |
| 118 | `GETCWD` | `char *buf, uint32_t size` | buf or error |

---

## Directory Operations

| # | Name | Arguments | Return |
|---|---|---|---|
| 37 | `FILE_MKDIR` | `const char *path` | 0 or error |
| 38 | `FILE_OPENDIR` | `const char *path` | handle or error |
| 39 | `FILE_READDIR` | `int32_t handle, void *out_entry` | 0/1 or error |
| 40 | `FILE_CLOSEDIR` | `int32_t handle` | 0 or error |
| 41 | `FILE_UNLINK` | `const char *path` | 0 or error |

---

## Memory Management

| # | Name | Arguments | Return |
|---|---|---|---|
| 27 | `USER_MALLOC` | `uint32_t size` | pointer or NULL |
| 28 | `USER_FREE` | `void *ptr` | 0 or error |
| 29 | `USER_MEMCPY` | `void *dst, const void *src, uint32_t size` | dst |
| 30 | `USER_MEMCMP` | `const void *a, const void *b, uint32_t size` | comparison result |
| 31 | `USER_MEMSET` | `void *ptr, uint8_t val, uint32_t size` | ptr |
| 43 | `USER_MMAP` | `uint64_t length, uint64_t flags` | address or error |

---

## Input

| # | Name | Arguments | Return |
|---|---|---|---|
| 32 | `INPUT_READ_KEYBOARD` | `void *out_event` | 1 if event, 0 if empty |
| 33 | `INPUT_READ_MOUSE` | `void *out_event` | 1 if event, 0 if empty |

### Keyboard Event Structure (`ps2_keyboard_event_t`)

```c
struct __attribute__((packed)) {
    uint16_t keycode;
    uint8_t  pressed;     // 1 = press, 0 = release
    uint8_t  ascii;       // ASCII character (0 if non-printable)
    uint8_t  modifiers;   // PS2_KBD_MOD_SHIFT/CTRL/ALT/CAPS
    uint8_t  reserved[3];
};
```

### Mouse Event Structure (`ps2_mouse_event_t`)

```c
struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint8_t  buttons;     // button bitmask
    int8_t   wheel;       // scroll delta
    uint8_t  reserved[2];
};
```

---

## IPC (Inter-Process Communication)

| # | Name | Arguments | Return |
|---|---|---|---|
| 45 | `IPC_SEND_MESSAGE` | `int32_t target_pid, const void *msg, uint32_t size` | `os_status_t` |
| 46 | `IPC_RECEIVE_MESSAGE` | `void *out_msg` | `os_status_t` |

Message maximum size: 256 bytes. Queue depth: 16 messages per process.

---

## Display & Window

| # | Name | Arguments | Return |
|---|---|---|---|
| 48 | `GET_DISPLAY_WIDTH` | — | width in pixels |
| 49 | `GET_DISPLAY_HEIGHT` | — | height in pixels |
| 50 | `WINDOW_REGISTER_SERVICE` | — | 0 or error |
| 51 | `WINDOW_GET_WM_PID` | — | WM PID |
| 52 | `DISPLAY_DRAW_PIXEL` | `x, y, color` | 0 |
| 53 | `DISPLAY_FILL_RECT` | `x, y, w, h, color` | 0 |
| 54 | `DISPLAY_PRESENT` | — | 0 |

---

## Network

| # | Name | Arguments | Return |
|---|---|---|---|
| 100 | `UDP_SEND` | `uint32_t dst_ip, uint16_t dst_port, const void *data, uint16_t len` | 0 or error |
| 101 | `TCP_CONNECT` | `uint32_t ip, uint16_t port` | fd or error |
| 102 | `TCP_LISTEN` | `uint16_t port` | fd or error |
| 103 | `TCP_ACCEPT` | `int32_t listen_fd` | fd or error |
| 104 | `TCP_SEND` | `int32_t fd, const void *data, uint32_t len` | bytes or error |
| 105 | `TCP_RECV` | `int32_t fd, void *buf, uint32_t len` | bytes or error |
| 106 | `TCP_CLOSE` | `int32_t fd` | 0 or error |
| 107 | `TCP_GET_STATE` | `int32_t fd` | state or error |
| 130 | `SOCKET_CREATE` | `int32_t type` | fd or error |
| 131 | `SOCKET_CONNECT` | `int32_t fd, uint32_t ip, uint16_t port` | 0 or error |
| 132 | `SOCKET_BIND` | `int32_t fd, uint16_t port` | 0 or error |
| 133 | `SOCKET_LISTEN` | `int32_t fd` | 0 or error |
| 134 | `SOCKET_ACCEPT` | `int32_t fd` | new_fd or error |
| 135 | `SOCKET_SEND` | `int32_t fd, const void *data, uint32_t len` | bytes or error |
| 136 | `SOCKET_RECV` | `int32_t fd, void *buf, uint32_t len` | bytes or error |
| 137 | `SOCKET_CLOSE` | `int32_t fd` | 0 or error |

---

## Time Utilities

| # | Name | Arguments | Return |
|---|---|---|---|
| 113 | `SLEEP` | `uint32_t ms` | 0 |
| 119 | `GET_UPTIME_MS` | — | uptime in ms |
| 120 | `NANOSLEEP` | `uint64_t ns` | 0 |

---

## Userland API Headers

Userland applications can use wrapper functions from `Userland/API/`:

| Header | Functions |
|---|---|
| `Process.h` | `process_spawn`, `process_yield`, `process_exit`, `process_get_pid` |
| `File.h` | `file_open`, `file_read`, `file_write`, `file_close`, `file_seek`, etc. |
| `Memory.h` | `user_malloc`, `user_free`, `user_mmap` |
| `Input.h` | `input_read_keyboard`, `input_read_mouse` |
| `IPC.h` | `ipc_send_message`, `ipc_receive_message` |
| `Graphics.h` | `display_width`, `display_height`, `draw_pixel`, etc. |
| `Window.h` | Window management wrappers |
| `Serial.h` | `serial_putchar`, `serial_puts` |
| `Network.h` | `udp_send` |
| `Socket.h` | `socket_create`, `socket_connect`, `socket_bind`, `socket_listen`, `socket_accept`, `socket_send`, `socket_recv`, `socket_close` |
| `Error.h` | `os_errno` |
