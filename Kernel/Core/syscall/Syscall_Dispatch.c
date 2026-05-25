#include "Syscall_Main.h"
#include "Syscall_File.h"
#include "kernel/status.h"
#include "Drivers/Module/DriverManager.h"
#include "Core/process/ProcessManager.h"
#include "IPC/IPC_Main.h"
#include "Core/window/WindowManager_Kernel.h"
#include "mmu/Paging_Main.h"
#include "Drivers/Module/DriverBinary.h"
#include "Debug/printf/printf.h"
#include "Debug/serial/Serial.h"
#include "Core/sync/Spinlock.h"
#include "Network/network_main.h"
#include "Network/udp/UDP.h"
#include "Network/tcp/TCP.h"
#include "Core/timer/Timer.h"
#include "Drivers/RTC/RTC.h"
#include "Core/vfs/VFS.h"
#include "kernel/config.h"

#include <stddef.h>
#include <stdint.h>

#define SYSCALL_MAX_PATH_LEN    512U
#define SYSCALL_MAX_PUTS_LEN    1024U
#define SYSCALL_MAX_IO_BYTES    (32ULL * 1024ULL * 1024ULL)
#define SYSCALL_MAX_MEM_BYTES   (128ULL * 1024ULL * 1024ULL)
#define SYSCALL_MAX_ALLOC_BYTES (128ULL * 1024ULL * 1024ULL)
#define SYSCALL_U32_MASK        0xFFFFFFFFULL
#define WM_FILL_RECT_TRACE      0

static const char k_decimal_digits[10] = "0123456789";

static void set_syscall_result(uint64_t saved_rsp, uint64_t value)
{
    uint64_t *frame = (uint64_t *)(uintptr_t)saved_rsp;
    frame[SYSCALL_FRAME_RAX] = value;
}

static void set_syscall_status(uint64_t saved_rsp, os_status_t status)
{
    set_syscall_result(saved_rsp, os_status_to_u64(status));
}

static void set_syscall_i32(uint64_t saved_rsp, int32_t value)
{
    set_syscall_result(saved_rsp, os_status_to_u64(os_status_from_i32(value)));
}

static int user_buffer_ok(const void *ptr, uint64_t len)
{
    if (len == 0) {
        return 1;
    }
    if (ptr == NULL) {
        return 0;
    }
    return process_user_buffer_is_valid(ptr, len);
}

static int copy_user_cstring(char *dst, uint64_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL || dst_size < 2ULL) {
        return -1;
    }

    uint64_t len = 0;
    if (process_user_cstring_length(src, dst_size - 1ULL, &len) < 0) {
        return -1;
    }
    if (len >= dst_size) {
        return -1;
    }

    for (uint64_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return 0;
}

static void syscall_fail(uint64_t saved_rsp,
                         uint64_t syscall_number,
                         os_status_t status,
                         const char *reason)
{
    set_syscall_status(saved_rsp, status);
}

static process_capability_mask_t syscall_required_capability(uint64_t syscall_number)
{
    switch (syscall_number) {
        case SYSCALL_SERIAL_PUTCHAR:
        case SYSCALL_SERIAL_PUTS:
        case SYSCALL_SERIAL_WRITE_U64:
        case SYSCALL_SERIAL_WRITE_U32:
        case SYSCALL_SERIAL_WRITE_U16:
            return PROCESS_CAP_SERIAL;

        case SYSCALL_PROCESS_CREATE:
        case SYSCALL_PROCESS_SPAWN_ELF:
        case SYSCALL_THREAD_CREATE:
            return PROCESS_CAP_PROCESS;
            
        case SYSCALL_GET_FAT32_FILE_T:
        case SYSCALL_FILE_OPEN:
        case SYSCALL_FILE_CREAT:
        case SYSCALL_FILE_READ:
        case SYSCALL_FILE_WRITE:
        case SYSCALL_FILE_CLOSE:
        case SYSCALL_FILE_SEEK:
        case SYSCALL_FILE_MKDIR:
        case SYSCALL_FILE_OPENDIR:
        case SYSCALL_FILE_READDIR:
        case SYSCALL_FILE_CLOSEDIR:
        case SYSCALL_FILE_UNLINK:
            return PROCESS_CAP_FILE;

        case SYSCALL_FILE_STAT:
        case SYSCALL_FILE_PIPE:
        case SYSCALL_FILE_DUP:
        case SYSCALL_FILE_DUP2:
            return PROCESS_CAP_FILE;

        case SYSCALL_USER_MMAP:
        case SYSCALL_USER_MALLOC:
        case SYSCALL_USER_FREE:
        case SYSCALL_USER_MEMCPY:
        case SYSCALL_USER_MEMCMP:
        case SYSCALL_USER_MEMSET:
            return PROCESS_CAP_MEMORY;

        case SYSCALL_INPUT_READ_KEYBOARD:
        case SYSCALL_INPUT_READ_MOUSE:
            return PROCESS_CAP_INPUT;

        case SYSCALL_PROCESS_SIGNAL:
            return PROCESS_CAP_SIGNAL;

        case SYSCALL_UDP_SEND:
        case SYSCALL_TCP_CONNECT:
        case SYSCALL_TCP_LISTEN:
        case SYSCALL_TCP_ACCEPT:
        case SYSCALL_TCP_SEND:
        case SYSCALL_TCP_RECV:
        case SYSCALL_TCP_CLOSE:
        case SYSCALL_TCP_GET_STATE:
            return PROCESS_CAP_NETWORK;

        case SYSCALL_IPC_SEND_MESSAGE:
        case SYSCALL_IPC_RECEIVE_MESSAGE:
            return PROCESS_CAP_IPC;

        case SYSCALL_PROCESS_YIELD:
        case SYSCALL_PROCESS_EXIT:
        case SYSCALL_PROCESS_GET_PID:
        case SYSCALL_PROCESS_WAITPID:
        case SYSCALL_PROCESS_GETPPID:
        case SYSCALL_PROCESS_EXIT_STATUS:
        case SYSCALL_SLEEP:
        case SYSCALL_NANOSLEEP:
        case SYSCALL_GET_UPTIME_MS:
        case SYSCALL_GETCWD:
        case SYSCALL_GET_PROC_COUNT:
        case SYSCALL_GET_PROC_INFO:
        default:
            return 0;
    }
}

uint64_t syscall_dispatch(uint64_t saved_rsp,
                         uint64_t num,
                         uint64_t arg1,
                         uint64_t arg2,
                         uint64_t arg3,
                         uint64_t arg4,
                         uint64_t arg5)
{
    __asm__ volatile("sti");

    (void)arg5;
    int32_t current_pid = current_pid_get();
    
    int request_switch = 0;
#if WM_FILL_RECT_TRACE
    int trace_wm_fill_rect = 0;
    if (num == SYSCALL_DISPLAY_FILL_RECT) {
        int32_t wm_pid = wm_kernel_get_wm_service_pid();
        trace_wm_fill_rect = (wm_pid >= 0 && current_pid == wm_pid);
    }
#endif

    if (driver_manager_input_usb_check_poll()) {
        uint64_t poll_flags = irq_save_disable();
        driver_manager_input_usb_poll();
        irq_restore(poll_flags);
    }

    if (network_stack_check_poll()) {
        uint64_t poll_flags = irq_save_disable();
        network_stack_poll();
        irq_restore(poll_flags);
    }

    if (num == SYSCALL_INPUT_READ_KEYBOARD || num == SYSCALL_INPUT_READ_MOUSE) {
        uint64_t poll_flags = irq_save_disable();
        driver_manager_input_ps2_poll();
        driver_manager_input_usb_poll();
        irq_restore(poll_flags);
    }

    process_capability_mask_t required_capability = syscall_required_capability(num);
    if (required_capability != 0 &&
        !process_current_has_capability(required_capability)) {
        syscall_fail(saved_rsp, num, OS_STATUS_ACCESS_DENIED, "capability_denied");
        goto pre_schedule;
    }

    switch (num) {
        case SYSCALL_SERIAL_PUTCHAR:
            serial_write_char((char)arg1);
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_SERIAL_PUTS: {
            char buffer[SYSCALL_MAX_PUTS_LEN];
            if (copy_user_cstring(buffer, sizeof(buffer),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_user_string");
                break;
            }
            serial_write_string(buffer);
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_SERIAL_WRITE_U64:
            serial_write_uint64(arg1);
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_SERIAL_WRITE_U32:
            serial_write_uint32(arg1);
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_SERIAL_WRITE_U16:
            serial_write_uint16(arg1);
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_PROCESS_CREATE: {
            int32_t pid = process_create_user(arg1);
            set_syscall_i32(saved_rsp, pid);
            break;
        }

        case SYSCALL_PROCESS_SPAWN_ELF: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }
            int32_t pid = process_spawn_user_elf(path);
            set_syscall_i32(saved_rsp, pid);
            break;
        }

        case SYSCALL_PROCESS_YIELD:
            set_syscall_result(saved_rsp, 0);
            request_switch = 1;
            break;

        case SYSCALL_PROCESS_EXIT:
            process_exit_current();
            request_switch = 1;
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_THREAD_CREATE: {
            int32_t tid = process_create_user_ex(arg1, arg2, arg3, arg4, arg5);
            set_syscall_i32(saved_rsp, tid);
            if (tid >= 0) {
                request_switch = 1;
            }
            break;
        }

        case SYSCALL_GET_FAT32_FILE_T: {
            set_syscall_i32(saved_rsp, (int32_t)sizeof(FAT32_FILE));
            break;
        }

        case SYSCALL_FILE_OPEN: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }

            int32_t fd = syscall_file_open(path, arg2);
            set_syscall_i32(saved_rsp, fd);
            break;
        }

        case SYSCALL_FILE_CREAT: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }

            int32_t fd = syscall_file_creat(path);
            set_syscall_i32(saved_rsp, fd);
            break;
        }

        case SYSCALL_FILE_READ: {
            if (arg3 > SYSCALL_MAX_IO_BYTES ||
                !user_buffer_ok((const void *)(uintptr_t)arg2, arg3)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_read_buffer");
                break;
            }

            int64_t n = syscall_file_read((int32_t)arg1,
                                          (uint8_t *)(uintptr_t)arg2, arg3);
            set_syscall_result(saved_rsp, (uint64_t)n);
            break;
        }

        case SYSCALL_FILE_WRITE: {
            if (arg3 > SYSCALL_MAX_IO_BYTES ||
                !user_buffer_ok((const void *)(uintptr_t)arg2, arg3)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_write_buffer");
                break;
            }

            int64_t n = syscall_file_write((int32_t)arg1,
                                           (const uint8_t *)(uintptr_t)arg2,
                                           arg3);
            set_syscall_result(saved_rsp, (uint64_t)n);
            break;
        }

        case SYSCALL_FILE_CLOSE: {
            int32_t rc = syscall_file_close((int32_t)arg1);
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_FILE_SEEK: {
            int64_t pos = syscall_file_seek((int32_t)arg1, (int64_t)arg2, (int32_t)arg3);
            set_syscall_result(saved_rsp, (uint64_t)pos);
            break;
        }

        case SYSCALL_FILE_MKDIR: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }

            int32_t rc = syscall_file_mkdir(path);
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_FILE_OPENDIR: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }

            int32_t handle = syscall_file_opendir(path);
            set_syscall_i32(saved_rsp, handle);
            break;
        }

        case SYSCALL_FILE_READDIR: {
            FAT32_DIRENT *entry_out = (FAT32_DIRENT *)(uintptr_t)arg2;
            if (!user_buffer_ok(entry_out, sizeof(FAT32_DIRENT))) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_dirent_buffer");
                break;
            }

            int32_t rc = syscall_file_readdir((int32_t)arg1, entry_out);
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_FILE_CLOSEDIR: {
            int32_t rc = syscall_file_closedir((int32_t)arg1);
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_FILE_UNLINK: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }

            int32_t rc = syscall_file_unlink(path);
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_USER_MMAP: {
            void *mapped = process_user_mmap(arg1, arg2);
            set_syscall_result(saved_rsp, (uint64_t)(uintptr_t)mapped);
            break;
        }

        case SYSCALL_PROCESS_SIGNAL: {
            uint64_t previous = process_signal_set_handler((int32_t)arg1, arg2);
            set_syscall_result(saved_rsp, previous);
            break;
        }

        case SYSCALL_UDP_SEND: {
            uint32_t dst_ip = (uint32_t)arg1;
            uint16_t src_port = (uint16_t)(arg2 >> 16);
            uint16_t dst_port = (uint16_t)(arg2 & 0xFFFF);
            const void *payload = (const void *)(uintptr_t)arg3;
            uint16_t payload_len = (uint16_t)arg4;
            bool success = udp_syscall_send(dst_ip, src_port, dst_port, payload, payload_len);
            set_syscall_result(saved_rsp, success ? 1ULL : 0ULL);
            break;
        }

        case SYSCALL_UDP_BIND: {
            uint16_t port = (uint16_t)arg1;
            int32_t pid = process_get_current_pid();
            int32_t result = udp_user_bind(pid, port);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_UDP_UNBIND: {
            uint16_t port = (uint16_t)arg1;
            int32_t pid = process_get_current_pid();
            int32_t result = udp_user_unbind(pid, port);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_UDP_RECV: {
            uint16_t port = (uint16_t)arg1;
            void *buf = (void *)(uintptr_t)arg2;
            uint32_t buf_len = (uint32_t)arg3;
            if (!user_buffer_ok(buf, buf_len)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_udp_recv_buffer");
                break;
            }
            int32_t pid = process_get_current_pid();
            int32_t result = udp_user_recv(pid, port, (uint8_t *)buf, buf_len);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_CONNECT: {
            uint32_t remote_ip = (uint32_t)arg1;
            uint16_t remote_port = (uint16_t)(arg2 >> 16);
            uint16_t local_port = (uint16_t)(arg2 & 0xFFFF);
            int32_t result = tcp_connect(remote_ip, remote_port, local_port);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_LISTEN: {
            uint16_t port = (uint16_t)arg1;
            int32_t result = tcp_listen(port);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_ACCEPT: {
            int32_t listen_id = (int32_t)arg1;
            int32_t result = tcp_accept(listen_id);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_SEND: {
            int32_t conn_id = (int32_t)arg1;
            const void *payload = (const void *)(uintptr_t)arg2;
            uint16_t payload_len = (uint16_t)arg3;
            if (!user_buffer_ok(payload, payload_len)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_tcp_send_buffer");
                break;
            }
            int32_t result = tcp_send(conn_id, payload, payload_len);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_RECV: {
            int32_t conn_id = (int32_t)arg1;
            void *buf = (void *)(uintptr_t)arg2;
            uint16_t buf_len = (uint16_t)arg3;
            if (!user_buffer_ok(buf, buf_len)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_tcp_recv_buffer");
                break;
            }
            int32_t result = tcp_recv(conn_id, buf, buf_len);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_CLOSE: {
            int32_t conn_id = (int32_t)arg1;
            int32_t result = tcp_close(conn_id);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_TCP_GET_STATE: {
            int32_t conn_id = (int32_t)arg1;
            tcp_state_t state = tcp_get_state(conn_id);
            set_syscall_result(saved_rsp, (uint64_t)state);
            break;
        }

        case SYSCALL_USER_MALLOC: {
            uint32_t size = (uint32_t)arg1;
            if (size == 0 || size > SYSCALL_MAX_ALLOC_BYTES) {
                set_syscall_result(saved_rsp, 0);
                break;
            }

            void *ptr = process_user_alloc(size);
            set_syscall_result(saved_rsp, (uint64_t)(uintptr_t)ptr);
            break;
        }

        case SYSCALL_USER_FREE: {
            int rc = process_user_free((void *)(uintptr_t)arg1);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)rc);
            break;
        }

        case SYSCALL_USER_MEMCPY: {
            void *dst = (void *)(uintptr_t)arg1;
            const void *src = (const void *)(uintptr_t)arg2;
            uint64_t n = arg3;

            if (n > SYSCALL_MAX_MEM_BYTES ||
                !user_buffer_ok(dst, n) ||
                !user_buffer_ok(src, n)) {
                set_syscall_result(saved_rsp, 0);
                break;
            }

            uint8_t *d = (uint8_t *)dst;
            const uint8_t *s = (const uint8_t *)src;
            
            uint64_t head = 0;
            while (head < n && ((uintptr_t)(d + head) & 7) != 0) {
                d[head] = s[head];
                head++;
            }
            uint64_t remaining = n - head;
            uint64_t words = remaining / 8;
            uint64_t tail  = remaining % 8;
            uint64_t *dw = (uint64_t *)(d + head);
            const uint64_t *sw = (const uint64_t *)(s + head);
            for (uint64_t i = 0; i < words; ++i)
                dw[i] = sw[i];
            uint8_t *dt = (uint8_t *)(dw + words);
            const uint8_t *st = (const uint8_t *)(sw + words);
            for (uint64_t i = 0; i < tail; ++i)
                dt[i] = st[i];

            set_syscall_result(saved_rsp, (uint64_t)(uintptr_t)dst);
            break;
        }

        case SYSCALL_USER_MEMCMP: {
            const uint8_t *s1 = (const uint8_t *)(uintptr_t)arg1;
            const uint8_t *s2 = (const uint8_t *)(uintptr_t)arg2;
            uint64_t n = arg3;

            if (n > SYSCALL_MAX_MEM_BYTES ||
                !user_buffer_ok(s1, n) ||
                !user_buffer_ok(s2, n)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_memcmp_buffer");
                break;
            }

            int result = 0;
            for (uint64_t i = 0; i < n; ++i) {
                if (s1[i] != s2[i]) {
                    result = (int)s1[i] - (int)s2[i];
                    break;
                }
            }

            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_USER_MEMSET: {
            uint8_t *dst = (uint8_t *)(uintptr_t)arg1;
            uint8_t value = (uint8_t)arg2;
            uint64_t n = arg3;

            if (n > SYSCALL_MAX_MEM_BYTES || !user_buffer_ok(dst, n)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_memset_buffer");
                break;
            }

            
            uint64_t head = 0;
            while (head < n && ((uintptr_t)(dst + head) & 7) != 0) {
                dst[head] = value;
                head++;
            }
            uint64_t fill64 = (uint64_t)value * 0x0101010101010101ULL;
            uint64_t remaining = n - head;
            uint64_t words = remaining / 8;
            uint64_t tail  = remaining % 8;
            uint64_t *wp = (uint64_t *)(dst + head);
            for (uint64_t i = 0; i < words; ++i)
                wp[i] = fill64;
            uint8_t *tp = (uint8_t *)(wp + words);
            for (uint64_t i = 0; i < tail; ++i)
                tp[i] = value;

            set_syscall_result(saved_rsp, arg1);
            break;
        }
        case SYSCALL_INPUT_READ_KEYBOARD: {
            driver_keyboard_event_t *event_out = (driver_keyboard_event_t *)(uintptr_t)arg1;
            if (!user_buffer_ok(event_out, sizeof(driver_keyboard_event_t))) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_keyboard_buffer");
                break;
            }
            int32_t rc = 0;
            if (!wm_kernel_is_running()) {
                rc = driver_manager_input_usb_read_keyboard(event_out);
                if (rc == 0) {
                    rc = driver_manager_input_ps2_read_keyboard(event_out);
                }
            }
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_INPUT_READ_MOUSE: {
            driver_mouse_event_t *event_out = (driver_mouse_event_t *)(uintptr_t)arg1;
            if (!user_buffer_ok(event_out, sizeof(driver_mouse_event_t))) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_mouse_buffer");
                break;
            }
            int32_t rc = 0;
            if (!wm_kernel_is_running()) {
                rc = driver_manager_input_usb_read_mouse(event_out);
                if (rc == 0) {
                    rc = driver_manager_input_ps2_read_mouse(event_out);
                }
            }
            set_syscall_i32(saved_rsp, rc);
            break;
        }

        case SYSCALL_IPC_SEND_MESSAGE: {
            int32_t target_pid = (int32_t)arg1;
            const void *message = (const void *)(uintptr_t)arg2;
            uint32_t size = (uint32_t)arg3;

            if (size > IPC_MESSAGE_MAX_SIZE || !user_buffer_ok(message, size)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_message_buffer");
                break;
            }

            os_status_t status = ipc_send_message(target_pid, message, size);
            set_syscall_status(saved_rsp, status);
            break;
        }

        case SYSCALL_IPC_RECEIVE_MESSAGE: {
            ipc_message_t *out_message = (ipc_message_t *)(uintptr_t)arg1;
            if (!user_buffer_ok(out_message, sizeof(ipc_message_t))) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_message_buffer");
                break;
            }

            os_status_t status = ipc_receive_message(out_message);
            set_syscall_status(saved_rsp, status);
            break;
        }

        case SYSCALL_PROCESS_GET_PID: {
            int32_t pid = process_get_current_pid();
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)pid);
            break;
        }

        case SYSCALL_GET_DISPLAY_WIDTH: {
            uint32_t width = driver_manager_display_width();
            set_syscall_result(saved_rsp, (uint64_t)width);
            break;
        }

        case SYSCALL_GET_DISPLAY_HEIGHT: {
            uint32_t height = driver_manager_display_height();
            set_syscall_result(saved_rsp, (uint64_t)height);
            break;
        }

        case SYSCALL_WINDOW_REGISTER_SERVICE: {
            wm_kernel_register_service(current_pid);
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_WINDOW_GET_WM_PID: {
            int32_t wm_pid = wm_kernel_get_wm_service_pid();
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)wm_pid);
            break;
        }

        case SYSCALL_DISPLAY_DRAW_PIXEL:
            driver_manager_display_draw_pixel((uint32_t)arg1,
                                              (uint32_t)arg2,
                                              (uint32_t)arg3);
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_DISPLAY_FILL_RECT:
            driver_manager_display_fill_rect((uint32_t)arg1,
                                             (uint32_t)arg2,
                                             (uint32_t)arg3,
                                             (uint32_t)arg4,
                                             (uint32_t)arg5);
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_DISPLAY_PRESENT:
            driver_manager_display_present();
            set_syscall_result(saved_rsp, 0);
            break;

        case SYSCALL_GET_DISPLAY_FRAMEBUFFER: {
            void *ptr = driver_manager_display_get_framebuffer();
            if (ptr != NULL) {
                uint64_t cr3 = process_get_current_cr3();
                uint32_t w = driver_manager_display_width();
                uint32_t h = driver_manager_display_height();
                uint64_t size = (uint64_t)w * (uint64_t)h * 4ULL;
                uint64_t start = (uint64_t)(uintptr_t)ptr;
                uint64_t end = start + size;
                uint64_t aligned_start = start & ~4095ULL;
                uint64_t aligned_end = (end + 4095ULL) & ~4095ULL;

                int res = paging_set_user_access(cr3, aligned_start, aligned_end - aligned_start, 1);
            }
            set_syscall_result(saved_rsp, (uint64_t)(uintptr_t)ptr);
            break;
        }

        case SYSCALL_DISPLAY_GET_PIXEL: {
            uint32_t color = driver_manager_display_get_pixel((uint32_t)arg1, (uint32_t)arg2);
            set_syscall_result(saved_rsp, (uint64_t)color);
            break;
        }

        case SYSCALL_SYSTEM_SHUTDOWN: {
            extern void acpi_shutdown(void);
            acpi_shutdown();
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_SYSTEM_SHUTDOWN_BROADCAST: {
            extern void process_broadcast_shutdown(void);
            process_broadcast_shutdown();
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_SYSTEM_REBOOT: {
            extern void acpi_reboot(void);
            acpi_reboot();
            set_syscall_result(saved_rsp, 0);
            break;
        }
        case SYSCALL_PROCESS_WAITPID: {
            int32_t *status_ptr = (int32_t *)(uintptr_t)arg2;
            if (status_ptr != NULL && !user_buffer_ok(status_ptr, sizeof(int32_t))) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_status_buffer");
                break;
            }
            int32_t result = process_waitpid((int32_t)arg1, status_ptr, (int32_t)arg3);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_GET_RTC_TIME: {
            rtc_time_t *out = (rtc_time_t *)(uintptr_t)arg1;
            if (!user_buffer_ok(out, sizeof(rtc_time_t))) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_rtc_buffer");
                break;
            }
            rtc_read_time(out);
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_PROCESS_GETPPID: {
            int32_t ppid = process_getppid();
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)ppid);
            break;
        }

        case SYSCALL_PROCESS_EXIT_STATUS: {
            process_exit_current_with_status((int32_t)arg1);
            request_switch = 1;
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_SLEEP: {
            uint64_t ms = arg1;
            if (ms > 0) {
                timer_apic_sleep_ms((uint32_t)ms);
            }
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_NANOSLEEP: {
            uint64_t ns = arg1;
            uint64_t ms = (ns + 999999) / 1000000;
            if (ms > 0) {
                request_switch = 1;
            }
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_GET_UPTIME_MS: {
            uint32_t hz = timer_hz();
            if (hz == 0) hz = 60;
            uint64_t ticks = timer_ticks();
            uint64_t ms = (ticks * 1000) / hz;
            set_syscall_result(saved_rsp, ms);
            break;
        }

        case SYSCALL_FILE_STAT: {
            char path[SYSCALL_MAX_PATH_LEN];
            if (copy_user_cstring(path, sizeof(path),
                                  (const char *)(uintptr_t)arg1) < 0) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_path");
                break;
            }

            struct { uint32_t size; uint8_t is_dir; uint8_t exists; } *stat_out =
                (void *)(uintptr_t)arg2;
            if (!user_buffer_ok(stat_out, 6)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_stat_buffer");
                break;
            }
            VFS_FILE vf;
            if (vfs_find_file(path, &vf)) {
                stat_out->size = vf.size;
                stat_out->is_dir = (vf.attributes & 0x10) ? 1 : 0;
                stat_out->exists = 1;
                set_syscall_result(saved_rsp, 0);
            } else {
                stat_out->size = 0;
                stat_out->is_dir = 0;
                stat_out->exists = 0;
                set_syscall_result(saved_rsp, (uint64_t)(int64_t)OS_STATUS_NOT_FOUND);
            }
            break;
        }

        case SYSCALL_GET_PROC_COUNT: {
            set_syscall_result(saved_rsp, (uint64_t)OS_CONFIG_PROCESS_MAX_COUNT);
            break;
        }

        case SYSCALL_GET_PROC_INFO: {
            int32_t query_pid = (int32_t)arg1;
            struct { int32_t pid; int32_t parent_pid; uint8_t state; uint8_t reserved[3]; } *info_out =
                (void *)(uintptr_t)arg2;
            if (!user_buffer_ok(info_out, 12)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_info_buffer");
                break;
            }
            if (query_pid < 0 || query_pid >= (int32_t)OS_CONFIG_PROCESS_MAX_COUNT) {
                set_syscall_result(saved_rsp, (uint64_t)(int64_t)OS_STATUS_NOT_FOUND);
                break;
            }
            int alive = process_is_alive(query_pid);
            if (!alive) {
                info_out->pid = query_pid;
                info_out->parent_pid = -1;
                info_out->state = 0;
                set_syscall_result(saved_rsp, (uint64_t)(int64_t)OS_STATUS_NOT_FOUND);
            } else {
                info_out->pid = query_pid;
                info_out->parent_pid = process_get_parent_pid(query_pid);
                info_out->state = 1;
                set_syscall_result(saved_rsp, 0);
            }
            break;
        }

        case SYSCALL_FILE_PIPE: {
            int32_t *fds_ptr = (int32_t *)(uintptr_t)arg1;
            if (!user_buffer_ok(fds_ptr, sizeof(int32_t) * 2)) {
                syscall_fail(saved_rsp, num, OS_STATUS_FAULT, "invalid_pipe_buffer");
                break;
            }
            int32_t result = syscall_file_pipe(fds_ptr);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_FILE_DUP: {
            int32_t result = syscall_file_dup((int32_t)arg1);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_FILE_DUP2: {
            int32_t result = syscall_file_dup2((int32_t)arg1, (int32_t)arg2);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_SOCKET_CREATE: {
            int32_t type = (int32_t)arg1;
            if (type == 1) {
                set_syscall_result(saved_rsp, 1);
            } else {
                set_syscall_result(saved_rsp, (uint64_t)(int64_t)OS_STATUS_NOT_SUPPORTED);
            }
            break;
        }

        case SYSCALL_SOCKET_CONNECT: {
            uint32_t ip = (uint32_t)arg1;
            uint16_t port = (uint16_t)arg2;
            uint16_t local_port = (uint16_t)(49152u + (timer_ticks() % 16384u));
            int32_t conn = tcp_connect(ip, port, local_port);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)conn);
            break;
        }

        case SYSCALL_SOCKET_BIND: {
            uint16_t port = (uint16_t)arg2;
            int32_t listen_id = tcp_listen(port);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)listen_id);
            break;
        }

        case SYSCALL_SOCKET_LISTEN: {
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_SOCKET_ACCEPT: {
            int32_t child = tcp_accept((int32_t)arg1);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)child);
            break;
        }

        case SYSCALL_SOCKET_SEND: {
            int32_t sent = tcp_send((int32_t)arg1, (const void *)(uintptr_t)arg2,
                                    (uint16_t)arg3);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)sent);
            break;
        }

        case SYSCALL_SOCKET_RECV: {
            int32_t recvd = tcp_recv((int32_t)arg1, (void *)(uintptr_t)arg2,
                                     (uint16_t)arg3);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)recvd);
            break;
        }

        case SYSCALL_SOCKET_CLOSE: {
            int32_t result = tcp_close((int32_t)arg1);
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)result);
            break;
        }

        case SYSCALL_MPROTECT: {
            extern int64_t syscall_vm_mprotect(uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_vm_mprotect(arg1, arg2, arg3));
            break;
        }
        case SYSCALL_MUNMAP: {
            extern int64_t syscall_vm_munmap(uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_vm_munmap(arg1, arg2));
            break;
        }
        case SYSCALL_MREMAP: {
            extern int64_t syscall_vm_mremap(uint64_t, uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_vm_mremap(arg1, arg2, arg3, arg4));
            break;
        }

        case SYSCALL_GETUID:
        case SYSCALL_GETEUID:
        case SYSCALL_GETGID:
        case SYSCALL_GETEGID:
            set_syscall_result(saved_rsp, 0);
            break;
        case SYSCALL_GETTID: {
            extern int64_t syscall_gettid(void);
            set_syscall_result(saved_rsp, (uint64_t)syscall_gettid());
            break;
        }
        case SYSCALL_SET_TID_ADDRESS: {
            extern int64_t syscall_set_tid_address(uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_set_tid_address(arg1));
            break;
        }

        case SYSCALL_EPOLL_CREATE: {
            extern int32_t syscall_epoll_create(uint64_t);
            set_syscall_i32(saved_rsp, syscall_epoll_create(arg1));
            break;
        }
        case SYSCALL_EPOLL_CTL: {
            extern int32_t syscall_epoll_ctl(int32_t, int32_t, int32_t, const void *);
            set_syscall_i32(saved_rsp, syscall_epoll_ctl((int32_t)arg1, (int32_t)arg2,
                            (int32_t)arg3, (const void *)(uintptr_t)arg4));
            break;
        }
        case SYSCALL_EPOLL_WAIT: {
            extern int32_t syscall_epoll_wait(int32_t, void *, int32_t, int32_t);
            set_syscall_i32(saved_rsp, syscall_epoll_wait((int32_t)arg1,
                            (void *)(uintptr_t)arg2, (int32_t)arg3, (int32_t)arg4));
            break;
        }
        case SYSCALL_EVENTFD: {
            extern int32_t syscall_eventfd(uint64_t, uint64_t);
            set_syscall_i32(saved_rsp, syscall_eventfd(arg1, arg2));
            break;
        }

        case SYSCALL_CLOCK_GETTIME: {
            extern int64_t syscall_clock_gettime(int32_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_clock_gettime((int32_t)arg1, arg2));
            break;
        }
        case SYSCALL_CLOCK_GETRES: {
            extern int64_t syscall_clock_getres(int32_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_clock_getres((int32_t)arg1, arg2));
            break;
        }

        case SYSCALL_ARCH_PRCTL: {
            extern int64_t syscall_arch_prctl(uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_arch_prctl(arg1, arg2));
            break;
        }
        case SYSCALL_PRLIMIT64: {
            extern int64_t syscall_prlimit64(uint64_t, uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_prlimit64(arg1, arg2, arg3, arg4));
            break;
        }
        case SYSCALL_GETRANDOM: {
            extern int64_t syscall_getrandom(uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_getrandom(arg1, arg2, arg3));
            break;
        }
        case SYSCALL_READV: {
            extern int64_t syscall_readv(int32_t, uint64_t, int32_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_readv((int32_t)arg1, arg2, (int32_t)arg3));
            break;
        }
        case SYSCALL_WRITEV: {
            extern int64_t syscall_writev(int32_t, uint64_t, int32_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_writev((int32_t)arg1, arg2, (int32_t)arg3));
            break;
        }
        case SYSCALL_FTRUNCATE: {
            extern int64_t syscall_ftruncate(int32_t, int64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_ftruncate((int32_t)arg1, (int64_t)arg2));
            break;
        }
        case SYSCALL_FCHMOD: {
            set_syscall_result(saved_rsp, 0);
            break;
        }
        case SYSCALL_RENAME: {
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)-38);
            break;
        }
        case SYSCALL_IOCTL_EX: {
            extern int64_t syscall_ioctl_ex(int32_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_ioctl_ex((int32_t)arg1, arg2, arg3));
            break;
        }
        case SYSCALL_FCNTL_EX: {
            extern int64_t syscall_fcntl_ex(int32_t, int32_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_fcntl_ex((int32_t)arg1, (int32_t)arg2, arg3));
            break;
        }
        case SYSCALL_ACCESS: {
            set_syscall_result(saved_rsp, 0);
            break;
        }
        case SYSCALL_SET_ROBUST_LIST: {
            set_syscall_result(saved_rsp, 0);
            break;
        }

        case SYSCALL_FUTEX: {
            extern int64_t syscall_futex(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_futex(arg1, arg2, arg3, arg4, arg5, 0));
            break;
        }

        case SYSCALL_CLONE: {
            int32_t tid = process_create_user_ex(arg1, arg2, arg3, arg4, arg5);
            set_syscall_i32(saved_rsp, tid);
            if (tid >= 0) request_switch = 1;
            break;
        }

        case SYSCALL_RT_SIGACTION: {
            extern int64_t syscall_rt_sigaction(uint64_t, uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_rt_sigaction(arg1, arg2, arg3, arg4));
            break;
        }
        case SYSCALL_RT_SIGPROCMASK: {
            extern int64_t syscall_rt_sigprocmask(uint64_t, uint64_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)syscall_rt_sigprocmask(arg1, arg2, arg3, arg4));
            break;
        }
        case SYSCALL_RT_SIGRETURN: {
            set_syscall_result(saved_rsp, 0);
            break;
        }
        case SYSCALL_SIGALTSTACK: {
            set_syscall_result(saved_rsp, 0);
            break;
        }
        case SYSCALL_TKILL: {
            set_syscall_result(saved_rsp, 0);
            break;
        }
        
        case SYSCALL_FORK: {
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)-38); 
            break;
        }
        case SYSCALL_EXECVE: {
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)-38);
            break;
        }
        case SYSCALL_VFORK: {
            set_syscall_result(saved_rsp, (uint64_t)(int64_t)-38);
            break;
        }

        case SYSCALL_DRM_OPEN: {
            extern int64_t drm_client_open(void);
            set_syscall_result(saved_rsp, (uint64_t)drm_client_open());
            break;
        }
        case SYSCALL_DRM_IOCTL: {
            extern int64_t drm_client_ioctl(int32_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)drm_client_ioctl((int32_t)arg1, arg2, arg3));
            break;
        }
        case SYSCALL_DRM_CLOSE: {
            extern int64_t drm_client_close(int32_t);
            set_syscall_result(saved_rsp, (uint64_t)drm_client_close((int32_t)arg1));
            break;
        }
        case SYSCALL_DRM_MMAP: {
            extern void *drm_client_mmap(int32_t, uint64_t, uint64_t);
            void *p = drm_client_mmap((int32_t)arg1, arg2, arg3);
            set_syscall_result(saved_rsp, (uint64_t)(uintptr_t)p);
            break;
        }

        case SYSCALL_EVDEV_OPEN: {
            extern int64_t evdev_open(const char*);
            set_syscall_result(saved_rsp, (uint64_t)evdev_open((const char*)(uintptr_t)arg1));
            break;
        }
        case SYSCALL_EVDEV_READ: {
            extern int64_t evdev_read(int32_t, void*, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)evdev_read((int32_t)arg1, (void*)(uintptr_t)arg2, arg3));
            break;
        }
        case SYSCALL_EVDEV_IOCTL: {
            extern int64_t evdev_ioctl(int32_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)evdev_ioctl((int32_t)arg1, arg2, arg3));
            break;
        }
        case SYSCALL_EVDEV_CLOSE: {
            extern int64_t evdev_close(int32_t);
            set_syscall_result(saved_rsp, (uint64_t)evdev_close((int32_t)arg1));
            break;
        }

        case SYSCALL_UNIX_SOCKET: {
            extern int64_t unix_socket_create(int32_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_create((int32_t)arg1));
            break;
        }
        case SYSCALL_UNIX_BIND: {
            extern int64_t unix_socket_bind(int32_t, const char*);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_bind((int32_t)arg1, (const char*)(uintptr_t)arg2));
            break;
        }
        case SYSCALL_UNIX_LISTEN: {
            extern int64_t unix_socket_listen(int32_t, int32_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_listen((int32_t)arg1, (int32_t)arg2));
            break;
        }
        case SYSCALL_UNIX_ACCEPT: {
            extern int64_t unix_socket_accept(int32_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_accept((int32_t)arg1));
            break;
        }
        case SYSCALL_UNIX_CONNECT: {
            extern int64_t unix_socket_connect(int32_t, const char*);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_connect((int32_t)arg1, (const char*)(uintptr_t)arg2));
            break;
        }
        case SYSCALL_UNIX_SEND: {
            extern int64_t unix_socket_send(int32_t, const void*, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_send((int32_t)arg1, (const void*)(uintptr_t)arg2, arg3));
            break;
        }
        case SYSCALL_UNIX_RECV: {
            extern int64_t unix_socket_recv(int32_t, void*, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_recv((int32_t)arg1, (void*)(uintptr_t)arg2, arg3));
            break;
        }
        case SYSCALL_UNIX_SENDMSG: {
            extern int64_t unix_socket_sendmsg(int32_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_sendmsg((int32_t)arg1, arg2));
            break;
        }
        case SYSCALL_UNIX_RECVMSG: {
            extern int64_t unix_socket_recvmsg(int32_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_recvmsg((int32_t)arg1, arg2));
            break;
        }
        case SYSCALL_UNIX_CLOSE: {
            extern int64_t unix_socket_close(int32_t);
            set_syscall_result(saved_rsp, (uint64_t)unix_socket_close((int32_t)arg1));
            break;
        }

        case SYSCALL_KVM_OPEN: {
            extern int64_t kvm_client_open(void);
            set_syscall_result(saved_rsp, (uint64_t)kvm_client_open());
            break;
        }
        case SYSCALL_KVM_IOCTL: {
            extern int64_t kvm_client_ioctl(int32_t, uint64_t, uint64_t);
            set_syscall_result(saved_rsp, (uint64_t)kvm_client_ioctl((int32_t)arg1, arg2, arg3));
            break;
        }
        case SYSCALL_KVM_CLOSE: {
            extern int64_t kvm_client_close(int32_t);
            set_syscall_result(saved_rsp, (uint64_t)kvm_client_close((int32_t)arg1));
            break;
        }
        case SYSCALL_KVM_MMAP: {
            extern void *kvm_client_mmap(int32_t, uint64_t, uint64_t);
            void *p = kvm_client_mmap((int32_t)arg1, arg2, arg3);
            if (p != NULL) {
                uint64_t cr3 = process_get_current_cr3();
                uint64_t start = (uint64_t)(uintptr_t)p;
                uint64_t aligned_start = start & ~4095ULL;
                uint64_t aligned_size = (arg3 + 4095ULL) & ~4095ULL;
                if (aligned_size == 0) aligned_size = 4096;
                paging_set_user_access(cr3, aligned_start, aligned_size, 1);
            }
            set_syscall_result(saved_rsp, (uint64_t)(uintptr_t)p);
            break;
        }

        default:
            syscall_fail(saved_rsp, num, OS_STATUS_NOT_SUPPORTED, "unknown_syscall");
            break;
    }

pre_schedule:
    __asm__ volatile("cli");

    if (process_timeslice_expired()) {
        request_switch = 1;
    }

    {
        uint64_t current_user_rsp = syscall_get_user_rsp();
        uint64_t next_user_rsp = current_user_rsp;
        uint64_t next_saved_rsp = process_schedule_on_syscall(saved_rsp,
                                                              current_user_rsp,
                                                              request_switch,
                                                              &next_user_rsp);
        if (!request_switch) {
            next_saved_rsp = saved_rsp;
            next_user_rsp = current_user_rsp;
        }

        syscall_set_user_rsp(next_user_rsp);

        return next_saved_rsp;
    }
}