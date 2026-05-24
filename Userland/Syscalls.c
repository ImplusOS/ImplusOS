#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/syscalls.h>
#include "Syscalls.h"
#include "API/File.h"
#include "API/WM_Protocol.h"
#include "API/Process.h"
#include "API/IPC.h"
#include "API/Graphics.h"
#include "API/Input.h"
#include "API/Window.h"
#include "API/Serial.h"

static uint32_t g_current_window_id = 0;
static uint32_t g_wm_request_id = 0;

#define MAX_INPUT_QUEUE             32

static input_keyboard_event_t g_kbd_queue[MAX_INPUT_QUEUE];
static uint32_t g_kbd_head = 0, g_kbd_tail = 0, g_kbd_count = 0;

static input_mouse_event_t g_mouse_queue[MAX_INPUT_QUEUE];
static uint32_t g_mouse_head = 0, g_mouse_tail = 0, g_mouse_count = 0;

static void handle_input_message(ipc_message_t *msg) {
    if (msg->size >= sizeof(wm_msg_header_t)) {
        wm_msg_header_t *hdr = (wm_msg_header_t *)msg->data;
        if (hdr->type == WM_KEYBOARD_EVENT) {
            if (g_kbd_count < MAX_INPUT_QUEUE) {
                memcpy(&g_kbd_queue[g_kbd_head], msg->data + sizeof(wm_msg_header_t), sizeof(input_keyboard_event_t));
                g_kbd_head = (g_kbd_head + 1) % MAX_INPUT_QUEUE;
                g_kbd_count++;
            }
        } else if (hdr->type == WM_MOUSE_EVENT) {
            if (g_mouse_count < MAX_INPUT_QUEUE) {
                memcpy(&g_mouse_queue[g_mouse_head], msg->data + sizeof(wm_msg_header_t), sizeof(input_mouse_event_t));
                g_mouse_head = (g_mouse_head + 1) % MAX_INPUT_QUEUE;
                g_mouse_count++;
            }
        }
    }
}

#define SYSCALL_SERIAL_PUTCHAR    1ULL
#define SYSCALL_SERIAL_PUTS       2ULL
#define SYSCALL_SERIAL_WRITE_U64  3ULL
#define SYSCALL_SERIAL_WRITE_U32  4ULL
#define SYSCALL_SERIAL_WRITE_U16  5ULL
#define SYSCALL_PROCESS_CREATE    6ULL
#define SYSCALL_PROCESS_YIELD     7ULL
#define SYSCALL_PROCESS_EXIT      8ULL
#define SYSCALL_THREAD_CREATE     9ULL
#define SYSCALL_GET_FAT32_FILE_T  60ULL
#define SYSCALL_FILE_OPEN         23ULL
#define SYSCALL_FILE_READ         24ULL
#define SYSCALL_FILE_WRITE        25ULL
#define SYSCALL_FILE_CLOSE        26ULL
#define SYSCALL_FILE_SEEK         34ULL
#define SYSCALL_USER_MALLOC      27ULL
#define SYSCALL_USER_FREE        28ULL
#define SYSCALL_USER_MEMCPY       29ULL
#define SYSCALL_USER_MEMCMP       30ULL
#define SYSCALL_USER_MEMSET       31ULL
#define SYSCALL_INPUT_READ_KEYBOARD 32ULL
#define SYSCALL_INPUT_READ_MOUSE  33ULL
#define SYSCALL_FILE_MKDIR        37ULL
#define SYSCALL_FILE_OPENDIR      38ULL
#define SYSCALL_FILE_READDIR      39ULL
#define SYSCALL_FILE_CLOSEDIR     40ULL
#define SYSCALL_FILE_UNLINK       41ULL
#define SYSCALL_FILE_CREAT        42ULL
#define SYSCALL_USER_MMAP         43ULL
#define SYSCALL_PROCESS_SIGNAL    44ULL
#define SYSCALL_IPC_SEND_MESSAGE  45ULL
#define SYSCALL_UDP_SEND          100ULL
#define SYSCALL_TCP_CONNECT       101ULL
#define SYSCALL_TCP_LISTEN        102ULL
#define SYSCALL_TCP_ACCEPT        103ULL
#define SYSCALL_TCP_SEND          104ULL
#define SYSCALL_TCP_RECV          105ULL
#define SYSCALL_TCP_CLOSE         106ULL
#define SYSCALL_TCP_GET_STATE     107ULL
#define SYSCALL_UDP_BIND          108ULL
#define SYSCALL_UDP_UNBIND        109ULL
#define SYSCALL_UDP_RECV          138ULL
#define SYSCALL_IPC_SEND_MESSAGE  45ULL
#define SYSCALL_IPC_RECEIVE_MESSAGE 46ULL
#define SYSCALL_PROCESS_GET_PID   47ULL
#define SYSCALL_GET_DISPLAY_WIDTH  48ULL
#define SYSCALL_GET_DISPLAY_HEIGHT 49ULL
#define SYSCALL_WINDOW_REGISTER_SERVICE 50ULL
#define SYSCALL_WINDOW_GET_WM_PID 51ULL
#define SYSCALL_DISPLAY_DRAW_PIXEL 52ULL
#define SYSCALL_DISPLAY_FILL_RECT 53ULL
#define SYSCALL_DISPLAY_PRESENT 54ULL
#define SYSCALL_GET_DISPLAY_FRAMEBUFFER 55ULL
#define SYSCALL_DISPLAY_GET_PIXEL  56ULL
#define SYSCALL_SYSTEM_SHUTDOWN    250ULL
#define SYSCALL_SYSTEM_REBOOT      251ULL
#define SYSCALL_PROCESS_SPAWN_ELF 36ULL

extern uint64_t syscall0(uint64_t num);
extern uint64_t syscall1(uint64_t num, uint64_t arg1);
extern uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern uint64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
extern uint64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5);

int os_get_errno(void)
{
    return os_errno;
}

void os_set_errno(int value)
{
    os_errno = (value < 0) ? -value : value;
}

void os_clear_errno(void)
{
    os_errno = 0;
}

int os_status_is_error(int64_t status_code)
{
    return status_code < 0;
}

int os_status_to_errno(int64_t status_code)
{
    if (status_code >= 0) {
        return 0;
    }

    switch (status_code) {
        case OS_STATUS_INVALID_ARG:
            return 22;
        case OS_STATUS_NOT_FOUND:
            return 2;
        case OS_STATUS_ACCESS_DENIED:
            return 13;
        case OS_STATUS_LIMIT_REACHED:
            return 24;
        case OS_STATUS_IO_ERROR:
            return 5;
        case OS_STATUS_FAULT:
            return 14;
        case OS_STATUS_NOT_SUPPORTED:
            return 95;
        case OS_STATUS_INTERNAL:
            return 255;
        default:
            if (status_code <= -4096LL) {
                return 255;
            }
            return (int)(-status_code);
    }
}

static int32_t os_errno_from_i32_status(int32_t value)
{
    if (os_status_is_error((int64_t)value)) {
        os_set_errno(os_status_to_errno((int64_t)value));
    } else {
        os_clear_errno();
    }
    return value;
}

static int64_t os_errno_from_i64_status(int64_t value)
{
    if (os_status_is_error(value)) {
        os_set_errno(os_status_to_errno(value));
    } else {
        os_clear_errno();
    }
    return value;
}

size_t os_strnlen(const char *str, size_t max_len)
{
    if (str == NULL) {
        return 0;
    }

    size_t len = 0;
    while (len < max_len && str[len] != '\0') {
        ++len;
    }
    return len;
}

int os_strcpy_s(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return -1;
    }

    size_t src_len = os_strnlen(src, dst_size);
    if (src_len >= dst_size) {
        dst[0] = '\0';
        return -1;
    }

    for (size_t i = 0; i <= src_len; ++i) {
        dst[i] = src[i];
    }
    return 0;
}

int os_strcat_s(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return -1;
    }

    size_t dst_len = os_strnlen(dst, dst_size);
    if (dst_len >= dst_size) {
        return -1;
    }

    size_t remaining = dst_size - dst_len;
    size_t src_len = os_strnlen(src, remaining);
    if (src_len >= remaining) {
        return -1;
    }

    for (size_t i = 0; i <= src_len; ++i) {
        dst[dst_len + i] = src[i];
    }
    return 0;
}

int32_t get_fat32_file_t()
{
    return os_errno_from_i32_status((int32_t)syscall0(SYSCALL_GET_FAT32_FILE_T));
}

__attribute__((unused)) int32_t file_open(const char *path, uint64_t flags)
{
    return os_errno_from_i32_status((int32_t)syscall2(SYSCALL_FILE_OPEN, (uint64_t)path, flags));
}

__attribute__((unused)) int32_t file_creat(const char *path)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_FILE_CREAT, (uint64_t)path));
}

__attribute__((unused)) int64_t file_read(int32_t fd, void *buffer, uint64_t len)
{
    return os_errno_from_i64_status((int64_t)syscall3(SYSCALL_FILE_READ, (uint64_t)fd, (uint64_t)buffer, len));
}

__attribute__((unused)) int64_t file_write(int32_t fd, const void *buffer, uint64_t len)
{
    return os_errno_from_i64_status((int64_t)syscall3(SYSCALL_FILE_WRITE, (uint64_t)fd, (uint64_t)buffer, len));
}

__attribute__((unused)) int32_t file_close(int32_t fd)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_FILE_CLOSE, (uint64_t)fd));
}

__attribute__((unused)) int32_t file_mkdir(const char *path)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_FILE_MKDIR, (uint64_t)path));
}

__attribute__((unused)) int32_t file_opendir(const char *path)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_FILE_OPENDIR, (uint64_t)path));
}

__attribute__((unused)) int32_t file_readdir(int32_t dir_handle, file_dirent_t *out_entry)
{
    return os_errno_from_i32_status((int32_t)syscall2(SYSCALL_FILE_READDIR, (uint64_t)dir_handle, (uint64_t)out_entry));
}

__attribute__((unused)) int32_t file_closedir(int32_t dir_handle)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_FILE_CLOSEDIR, (uint64_t)dir_handle));
}

__attribute__((unused)) int32_t file_unlink(const char *path)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_FILE_UNLINK, (uint64_t)path));
}

void *os_mmap(uint64_t length, uint64_t flags)
{
    void *ptr = (void *)(uintptr_t)syscall2(SYSCALL_USER_MMAP, length, flags);
    if (ptr == NULL && length != 0ULL) {
        os_set_errno(12);
    } else {
        os_clear_errno();
    }
    return ptr;
}

signal_handler_t signal(int32_t signum, signal_handler_t handler)
{
    uint64_t raw = syscall2(SYSCALL_PROCESS_SIGNAL,
                            (uint64_t)signum,
                            (uint64_t)(uintptr_t)handler);
    if (os_status_is_error((int64_t)raw)) {
        os_set_errno(os_status_to_errno((int64_t)raw));
        return (signal_handler_t)0;
    }
    os_clear_errno();
    return (signal_handler_t)(uintptr_t)raw;
}

__attribute__((unused)) int64_t file_seek(int32_t fd, int64_t offset, int32_t whence)
{
    return os_errno_from_i64_status((int64_t)syscall3(SYSCALL_FILE_SEEK,
                                                      (uint64_t)fd,
                                                      (uint64_t)offset,
                                                      (uint64_t)whence));
}

void serial_write_string(const char *str)
{
    (void)syscall1(SYSCALL_SERIAL_PUTS, (uint64_t)str);
}

void serial_write_uint64(uint64_t value)
{
    (void)syscall1(SYSCALL_SERIAL_WRITE_U64, (uint64_t)value);
}

void serial_write_uint32(uint32_t value)
{
    (void)syscall1(SYSCALL_SERIAL_WRITE_U32, (uint64_t)value);
}

void serial_write_uint16(uint16_t value)
{
    (void)syscall1(SYSCALL_SERIAL_WRITE_U16, (uint64_t)value);
}

void process_yield(void)
{
    (void)syscall0(SYSCALL_PROCESS_YIELD);
}

int32_t input_read_keyboard(input_keyboard_event_t *event_out)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_INPUT_READ_KEYBOARD, (uint64_t)event_out));
}

int32_t input_read_mouse(input_mouse_event_t *event_out)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_INPUT_READ_MOUSE, (uint64_t)event_out));
}

int32_t ipc_send_message(int32_t target_pid, const void *message, uint32_t size)
{
    if (size > IPC_MESSAGE_MAX_SIZE) {
        os_set_errno(22);
        return -1;
    }
    return os_errno_from_i32_status((int32_t)syscall3(SYSCALL_IPC_SEND_MESSAGE,
                                                      (uint64_t)target_pid,
                                                      (uint64_t)message,
                                                      (uint64_t)size));
}

int32_t ipc_receive_message(ipc_message_t *out_message)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_IPC_RECEIVE_MESSAGE, (uint64_t)out_message));
}

int32_t process_get_current_pid(void)
{
    return os_errno_from_i32_status((int32_t)syscall0(SYSCALL_PROCESS_GET_PID));
}

int32_t process_spawn(const char *path)
{
    return os_errno_from_i32_status((int32_t)syscall1(SYSCALL_PROCESS_SPAWN_ELF, (uint64_t)path));
}

int32_t window_register_service(void)
{
    return os_errno_from_i32_status((int32_t)syscall0(SYSCALL_WINDOW_REGISTER_SERVICE));
}

int32_t window_get_wm_pid(void)
{
    int32_t pid = (int32_t)syscall0(SYSCALL_WINDOW_GET_WM_PID);
    if (pid < 0) {
        os_set_errno(14);
        return -1;
    }
    return pid;
}

window_id_t window_create(uint32_t width, uint32_t height, const char *title)
{
    return window_create_ex(100, 100, width, height, 0xFF000000, title);
}

window_id_t window_create_ex(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                             uint32_t bg_color, const char *title)
{
    struct {
        wm_msg_header_t hdr;
        uint32_t width, height;
        uint32_t x, y;
        uint32_t bg_color;
        char title[64];
    } __attribute__((aligned(16))) cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = WM_CREATE_WINDOW;
    cmd.hdr.request_id = ++g_wm_request_id;
    cmd.width = width;
    cmd.height = height;
    cmd.x = x;
    cmd.y = y;
    cmd.bg_color = bg_color;
    
    if (title) {
        os_strcpy_s(cmd.title, sizeof(cmd.title), title);
    }

    int32_t wm_pid = window_get_wm_pid();
    if (ipc_send_message(wm_pid, &cmd, sizeof(cmd)) < 0) {
        return 0;
    }

    ipc_message_t resp __attribute__((aligned(16)));
    while (1) {
        if (ipc_receive_message(&resp) == 0) {
            wm_msg_header_t *hdr = (wm_msg_header_t *)resp.data;
            if (hdr->type == WM_WINDOW_CREATED && hdr->request_id == cmd.hdr.request_id) {
                return hdr->window_id;
            } else {
                handle_input_message(&resp);
            }
        }
        process_yield();
    }
}

void window_destroy(window_id_t wid)
{
    wm_msg_header_t hdr;
    hdr.type = WM_DESTROY_WINDOW;
    hdr.window_id = wid;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

void window_set_rect(window_id_t wid, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct {
        wm_msg_header_t hdr;
        uint32_t x, y, w, h;
    } cmd;
    cmd.hdr.type = WM_SET_WINDOW_RECT;
    cmd.hdr.window_id = wid;
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
    ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

void window_show(window_id_t wid)
{
    wm_msg_header_t hdr;
    hdr.type = WM_SHOW_WINDOW;
    hdr.window_id = wid;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

void window_hide(window_id_t wid)
{
    wm_msg_header_t hdr;
    hdr.type = WM_HIDE_WINDOW;
    hdr.window_id = wid;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

void window_raise(window_id_t wid)
{
    wm_msg_header_t hdr;
    hdr.type = WM_RAISE_WINDOW;
    hdr.window_id = wid;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

void window_lower(window_id_t wid)
{
    wm_msg_header_t hdr;
    hdr.type = WM_LOWER_WINDOW;
    hdr.window_id = wid;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

void window_set_focus(window_id_t wid)
{
    wm_msg_header_t hdr;
    hdr.type = WM_SET_FOCUS;
    hdr.window_id = wid;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

int32_t window_get_rect(window_id_t wid, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h)
{
    if (wid == 0 || x == NULL || y == NULL || w == NULL || h == NULL) {
        os_set_errno(22);
        return WM_STATUS_INVALID_ARG;
    }

    struct {
        wm_msg_header_t hdr;
    } cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = WM_GET_WINDOW_RECT;
    cmd.hdr.request_id = ++g_wm_request_id;
    cmd.hdr.window_id = wid;

    int32_t wm_pid = window_get_wm_pid();
    if (wm_pid < 0) {
        return -1;
    }
    if (ipc_send_message(wm_pid, &cmd, sizeof(cmd)) < 0) {
        return -1;
    }

    ipc_message_t resp __attribute__((aligned(16)));
    while (1) {
        if (ipc_receive_message(&resp) == 0) {
            if (resp.size >= sizeof(wm_msg_header_t) + sizeof(int32_t) + (sizeof(uint32_t) * 4U)) {
                struct {
                    wm_msg_header_t hdr;
                    int32_t status;
                    uint32_t x, y, w, h;
                } *reply = (void *)resp.data;

                if (reply->hdr.type == WM_GET_WINDOW_RECT &&
                    reply->hdr.request_id == cmd.hdr.request_id) {
                    if (reply->status != WM_STATUS_OK) {
                        return os_errno_from_i32_status(reply->status);
                    }
                    *x = reply->x;
                    *y = reply->y;
                    *w = reply->w;
                    *h = reply->h;
                    os_clear_errno();
                    return WM_STATUS_OK;
                } else {
                    handle_input_message(&resp);
                }
            } else {
                handle_input_message(&resp);
            }
        }
        process_yield();
    }
}

window_id_t window_get_focus(void)
{
    struct {
        wm_msg_header_t hdr;
    } cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = WM_GET_FOCUS;
    cmd.hdr.request_id = ++g_wm_request_id;
    cmd.hdr.window_id = 0;

    int32_t wm_pid = window_get_wm_pid();
    if (wm_pid < 0) {
        return 0;
    }
    if (ipc_send_message(wm_pid, &cmd, sizeof(cmd)) < 0) {
        return 0;
    }

    ipc_message_t resp __attribute__((aligned(16)));
    while (1) {
        if (ipc_receive_message(&resp) == 0) {
            if (resp.size >= sizeof(wm_msg_header_t) + sizeof(int32_t) + sizeof(uint32_t)) {
                struct {
                    wm_msg_header_t hdr;
                    int32_t status;
                    uint32_t focused_window_id;
                } *reply = (void *)resp.data;

                if (reply->hdr.type == WM_GET_FOCUS &&
                    reply->hdr.request_id == cmd.hdr.request_id) {
                    if (reply->status != WM_STATUS_OK) {
                        (void)os_errno_from_i32_status(reply->status);
                        return 0;
                    }
                    os_clear_errno();
                    return reply->focused_window_id;
                } else {
                    handle_input_message(&resp);
                }
            } else {
                handle_input_message(&resp);
            }
        }
        process_yield();
    }
}

int32_t window_subscribe_keyboard(window_id_t wid)
{
    struct {
        wm_msg_header_t hdr;
        uint32_t input_types;
    } cmd;
    cmd.hdr.type = WM_SUBSCRIBE_INPUT;
    cmd.hdr.window_id = wid;
    cmd.input_types = 1;
    return ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

int32_t window_subscribe_mouse(window_id_t wid)
{
    struct {
        wm_msg_header_t hdr;
        uint32_t input_types;
    } cmd;
    cmd.hdr.type = WM_SUBSCRIBE_INPUT;
    cmd.hdr.window_id = wid;
    cmd.input_types = 2;
    return ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

void window_set_system(window_id_t wid, bool is_system)
{
    struct {
        wm_msg_header_t hdr;
        bool is_system_flag;
    } cmd;
    cmd.hdr.type = WM_SET_WINDOW_SYSTEM;
    cmd.hdr.window_id = wid;
    cmd.is_system_flag = is_system;
    ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

int32_t window_unsubscribe_input(window_id_t wid)
{
    struct {
        wm_msg_header_t hdr;
        uint32_t input_types;
    } cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = WM_UNSUBSCRIBE_INPUT;
    cmd.hdr.window_id = wid;
    cmd.input_types = 0;
    return ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

void window_set_bg_color(window_id_t wid, uint32_t color)
{
    uint32_t win_x = 0;
    uint32_t win_y = 0;
    uint32_t win_w = 0;
    uint32_t win_h = 0;
    if (window_get_rect(wid, &win_x, &win_y, &win_w, &win_h) < 0) {
        return;
    }
    (void)win_x;
    (void)win_y;

    struct {
        wm_msg_header_t hdr;
        uint32_t x, y, w, h;
        uint32_t color_value;
    } cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = WM_DRAW_RECT;
    cmd.hdr.window_id = wid;
    cmd.x = 0;
    cmd.y = 0;
    cmd.w = win_w;
    cmd.h = win_h;
    cmd.color_value = color;
    (void)ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

void window_clear(window_id_t wid)
{
    if (wid == 0) {
        return;
    }

    wm_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = WM_CLEAR_WINDOW;
    hdr.window_id = wid;
    (void)ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

int32_t graphics_init(uint32_t window_id)
{
    g_current_window_id = window_id;
    return 0;
}

__attribute__((optimize("O2"))) void draw_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (g_current_window_id == 0) {
        (void)syscall3(SYSCALL_DISPLAY_DRAW_PIXEL,
                       (uint64_t)x,
                       (uint64_t)y,
                       (uint64_t)color);
        return;
    }

    struct {
        wm_msg_header_t hdr;
        uint32_t x, y;
        uint32_t color;
    } cmd;
    cmd.hdr.type = WM_DRAW_PIXEL;
    cmd.hdr.window_id = g_current_window_id;
    cmd.x = x; cmd.y = y; cmd.color = color;
    ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

__attribute__((optimize("O2"))) void draw_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (g_current_window_id == 0) {
        (void)syscall5(SYSCALL_DISPLAY_FILL_RECT,
                       (uint64_t)x,
                       (uint64_t)y,
                       (uint64_t)w,
                       (uint64_t)h,
                       (uint64_t)color);
        return;
    }

    struct {
        wm_msg_header_t hdr;
        uint32_t x, y, w, h;
        uint32_t color;
    } cmd;
    cmd.hdr.type = WM_DRAW_RECT;
    cmd.hdr.window_id = g_current_window_id;
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h; cmd.color = color;
    ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

uint32_t get_pixel(uint32_t x, uint32_t y)
{
    return (uint32_t)syscall2(SYSCALL_DISPLAY_GET_PIXEL, (uint64_t)x, (uint64_t)y);
}

__attribute__((optimize("O2"))) void draw_present(void)
{
    if (g_current_window_id == 0) {
        (void)syscall0(SYSCALL_DISPLAY_PRESENT);
        return;
    }

    wm_msg_header_t hdr;
    hdr.type = WM_UPDATE_COMPLETE;
    hdr.window_id = g_current_window_id;
    ipc_send_message(window_get_wm_pid(), &hdr, sizeof(hdr));
}

uint32_t get_display_width(void)
{
    return (uint32_t)syscall0(SYSCALL_GET_DISPLAY_WIDTH);
}

uint32_t get_display_height(void)
{
    return (uint32_t)syscall0(SYSCALL_GET_DISPLAY_HEIGHT);
}

void *sys_get_display_framebuffer(void)
{
    return (void *)(uintptr_t)syscall0(SYSCALL_GET_DISPLAY_FRAMEBUFFER);
}

int32_t window_input_keyboard_poll(input_keyboard_event_t *out)
{
    ipc_message_t msg;
    while (ipc_receive_message(&msg) == 0) {
        handle_input_message(&msg);
    }
    
    if (g_kbd_count > 0) {
        *out = g_kbd_queue[g_kbd_tail];
        g_kbd_tail = (g_kbd_tail + 1) % MAX_INPUT_QUEUE;
        g_kbd_count--;
        return 1;
    }
    return 0;
}

int32_t window_input_mouse_poll(input_mouse_event_t *out)
{
    ipc_message_t msg;
    while (ipc_receive_message(&msg) == 0) {
        handle_input_message(&msg);
    }

    if (g_mouse_count > 0) {
        *out = g_mouse_queue[g_mouse_tail];
        g_mouse_tail = (g_mouse_tail + 1) % MAX_INPUT_QUEUE;
        g_mouse_count--;
        return 1;
    }
    return 0;
}

int32_t window_input_keyboard_wait(input_keyboard_event_t *out)
{
    while (1) {
        if (window_input_keyboard_poll(out) > 0) return 1;
        process_yield();
    }
}

int32_t window_input_mouse_wait(input_mouse_event_t *out)
{
    while (1) {
        if (window_input_mouse_poll(out) > 0) return 1;
        process_yield();
    }
}

int32_t window_input_keyboard_pending(void)
{
    ipc_message_t msg;
    while (ipc_receive_message(&msg) == 0) {
        handle_input_message(&msg);
    }
    return (int32_t)g_kbd_count;
}

int32_t window_input_mouse_pending(void)
{
    ipc_message_t msg;
    while (ipc_receive_message(&msg) == 0) {
        handle_input_message(&msg);
    }
    return (int32_t)g_mouse_count;
}

int32_t window_set_layout_xml(window_id_t wid, const char *xml_str, uint32_t xml_len)
{
    if (wid == 0 || !xml_str) return -1;
    
    struct {
        uint32_t type;
        uint32_t request_id;
        uint32_t window_id;
        uint32_t total_size;
    } start_cmd;
    
    start_cmd.type = WM_SET_LAYOUT_XML_START;
    start_cmd.request_id = ++g_wm_request_id;
    start_cmd.window_id = wid;
    start_cmd.total_size = xml_len;
    
    int32_t wm_pid = window_get_wm_pid();
    int32_t res = ipc_send_message(wm_pid, &start_cmd, sizeof(start_cmd));
    
    while (res == -24) {
        process_yield();
        res = ipc_send_message(wm_pid, &start_cmd, sizeof(start_cmd));
    }
    if (res < 0) return res;
    
    uint32_t offset = 0;
    while (offset < xml_len) {
        char chunk_msg[IPC_MESSAGE_MAX_SIZE];
        wm_msg_header_t *hdr = (wm_msg_header_t *)chunk_msg;
        hdr->type = WM_SET_LAYOUT_XML_CHUNK;
        hdr->request_id = ++g_wm_request_id;
        hdr->window_id = wid;
        
        uint32_t chunk_capacity = IPC_MESSAGE_MAX_SIZE - sizeof(wm_msg_header_t);
        uint32_t copy_size = (xml_len - offset < chunk_capacity) ? (xml_len - offset) : chunk_capacity;
        
        memcpy(chunk_msg + sizeof(wm_msg_header_t), xml_str + offset, copy_size);
        
        res = ipc_send_message(wm_pid, chunk_msg, sizeof(wm_msg_header_t) + copy_size);
        while (res == -24) {
            process_yield();
            res = ipc_send_message(wm_pid, chunk_msg, sizeof(wm_msg_header_t) + copy_size);
        }
        if (res < 0) return res;
        
        offset += copy_size;
    }
    
    wm_msg_header_t end_cmd;
    end_cmd.type = WM_SET_LAYOUT_XML_END;
    end_cmd.request_id = ++g_wm_request_id;
    end_cmd.window_id = wid;
    
    res = ipc_send_message(wm_pid, &end_cmd, sizeof(end_cmd));
    while (res == -24) {
        process_yield();
        res = ipc_send_message(wm_pid, &end_cmd, sizeof(end_cmd));
    }
    return res;
}

int32_t window_load_layout(window_id_t wid, const char *xml_path)
{
    int32_t fd = file_open(xml_path, 0);
    if (fd < 0) return fd;
    
    char *xml_buf = malloc(4096);
    if (!xml_buf) {
        file_close(fd);
        return -1;
    }
    
    int64_t bytes = file_read(fd, xml_buf, 4095);
    file_close(fd);
    
    if (bytes <= 0) {
        free(xml_buf);
        return -1;
    }
    xml_buf[bytes] = '\0';
    
    int32_t res = window_set_layout_xml(wid, xml_buf, (uint32_t)bytes);
    free(xml_buf);
    return res;
}

void window_draw_text(window_id_t wid, uint32_t x, uint32_t y, const char *text, uint32_t color, float font_size)
{
    struct {
        wm_msg_header_t hdr;
        uint32_t x, y;
        uint32_t color;
        float font_size;
        char text[128];
    } cmd;

    if (!text || wid == 0) return;

    cmd.hdr.type = WM_DRAW_TEXT;
    cmd.hdr.window_id = wid;
    cmd.x = x;
    cmd.y = y;
    cmd.color = color;
    cmd.font_size = font_size;
    os_strcpy_s(cmd.text, sizeof(cmd.text), text);

    ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}

void window_show_notification(const char *title, const char *message)
{
    struct {
        wm_msg_header_t hdr;
        char title[64];
        char message[128];
    } cmd;

    if (!title || !message) return;

    cmd.hdr.type = WM_SHOW_NOTIFICATION;
    cmd.hdr.window_id = 0;
    os_strcpy_s(cmd.title, sizeof(cmd.title), title);
    os_strcpy_s(cmd.message, sizeof(cmd.message), message);

    ipc_send_message(window_get_wm_pid(), &cmd, sizeof(cmd));
}
bool udp_send(uint32_t dst_ipv4_addr,
              uint16_t src_port,
              uint16_t dst_port,
              const void *payload,
              uint16_t payload_len)
{
    uint64_t arg2 = ((uint64_t)src_port << 16) | dst_port;
    uint64_t result = syscall4(SYSCALL_UDP_SEND, 
                               (uint64_t)dst_ipv4_addr, 
                               arg2, 
                               (uint64_t)(uintptr_t)payload, 
                               (uint64_t)payload_len);
    return result != 0;
}

int32_t udp_bind_port(uint16_t port)
{
    return (int32_t)syscall1(SYSCALL_UDP_BIND, (uint64_t)port);
}

int32_t udp_unbind_port(uint16_t port)
{
    return (int32_t)syscall1(SYSCALL_UDP_UNBIND, (uint64_t)port);
}

int32_t udp_recv(uint16_t port, void *buf, uint32_t buf_len)
{
    return (int32_t)syscall3(SYSCALL_UDP_RECV,
                             (uint64_t)port,
                             (uint64_t)(uintptr_t)buf,
                             (uint64_t)buf_len);
}

int32_t tcp_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port)
{
    uint64_t arg2 = ((uint64_t)remote_port << 16) | (uint64_t)local_port;
    return (int32_t)syscall2(SYSCALL_TCP_CONNECT, (uint64_t)remote_ip, arg2);
}

int32_t tcp_listen(uint16_t port)
{
    return (int32_t)syscall1(SYSCALL_TCP_LISTEN, (uint64_t)port);
}

int32_t tcp_accept(int32_t listen_conn_id)
{
    return (int32_t)syscall1(SYSCALL_TCP_ACCEPT, (uint64_t)(int64_t)listen_conn_id);
}

int32_t tcp_send(int32_t conn_id, const void *data, uint16_t len)
{
    return (int32_t)syscall3(SYSCALL_TCP_SEND,
                             (uint64_t)(int64_t)conn_id,
                             (uint64_t)(uintptr_t)data,
                             (uint64_t)len);
}

int32_t tcp_recv(int32_t conn_id, void *buf, uint16_t buf_len)
{
    return (int32_t)syscall3(SYSCALL_TCP_RECV,
                             (uint64_t)(int64_t)conn_id,
                             (uint64_t)(uintptr_t)buf,
                             (uint64_t)buf_len);
}

int32_t tcp_close(int32_t conn_id)
{
    return (int32_t)syscall1(SYSCALL_TCP_CLOSE, (uint64_t)(int64_t)conn_id);
}

int32_t tcp_get_state(int32_t conn_id)
{
    return (int32_t)syscall1(SYSCALL_TCP_GET_STATE, (uint64_t)(int64_t)conn_id);
}

#define SYSCALL_PROCESS_WAITPID    110ULL
#define SYSCALL_PROCESS_GETPPID    111ULL
#define SYSCALL_PROCESS_EXIT_STATUS 112ULL
#define SYSCALL_SLEEP_MS           113ULL
#define SYSCALL_FILE_STAT_NUM      114ULL
#define SYSCALL_GET_UPTIME_MS_NUM  119ULL
#define SYSCALL_GET_PROC_COUNT_NUM 121ULL
#define SYSCALL_GET_PROC_INFO_NUM  122ULL
#define SYSCALL_GET_RTC_TIME       140ULL

int32_t sys_get_rtc_time(rtc_time_t *time)
{
    return (int32_t)syscall1(SYSCALL_GET_RTC_TIME, (uint64_t)time);
}

int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options)
{
    return (int32_t)syscall3(SYSCALL_PROCESS_WAITPID,
                             (uint64_t)(int64_t)pid,
                             (uint64_t)(uintptr_t)status_out,
                             (uint64_t)(int64_t)options);
}

int32_t process_getppid(void)
{
    return (int32_t)syscall0(SYSCALL_PROCESS_GETPPID);
}

void process_exit(int32_t status)
{
    (void)syscall1(SYSCALL_PROCESS_EXIT_STATUS, (uint64_t)(int64_t)status);
    while (1) { process_yield(); }
}

void system_shutdown(void)
{
    (void)syscall0(SYSCALL_SYSTEM_SHUTDOWN);
    while (1) { process_yield(); }
}

void system_reboot(void)
{
    (void)syscall0(SYSCALL_SYSTEM_REBOOT);
    while (1) { process_yield(); }
}

void sleep_ms(uint64_t ms)
{
    (void)syscall1(SYSCALL_SLEEP_MS, ms);
}

uint64_t get_uptime_ms(void)
{
    return syscall0(SYSCALL_GET_UPTIME_MS_NUM);
}

int32_t file_stat(const char *path, file_stat_t *stat_out)
{
    return os_errno_from_i32_status(
        (int32_t)syscall2(SYSCALL_FILE_STAT_NUM,
                          (uint64_t)(uintptr_t)path,
                          (uint64_t)(uintptr_t)stat_out));
}

int32_t get_process_count(void)
{
    return (int32_t)syscall0(SYSCALL_GET_PROC_COUNT_NUM);
}

int32_t get_process_info(int32_t pid, process_info_t *info_out)
{
    return os_errno_from_i32_status(
        (int32_t)syscall2(SYSCALL_GET_PROC_INFO_NUM,
                          (uint64_t)(int64_t)pid,
                          (uint64_t)(uintptr_t)info_out));
}

#define SYSCALL_FILE_PIPE_NUM  115ULL
#define SYSCALL_FILE_DUP_NUM   116ULL
#define SYSCALL_FILE_DUP2_NUM  117ULL

int32_t file_pipe(int32_t fds[2])
{
    return os_errno_from_i32_status(
        (int32_t)syscall1(SYSCALL_FILE_PIPE_NUM,
                          (uint64_t)(uintptr_t)fds));
}

int32_t file_dup(int32_t oldfd)
{
    return (int32_t)syscall1(SYSCALL_FILE_DUP_NUM, (uint64_t)(int64_t)oldfd);
}

int32_t file_dup2(int32_t oldfd, int32_t newfd)
{
    return (int32_t)syscall2(SYSCALL_FILE_DUP2_NUM,
                             (uint64_t)(int64_t)oldfd,
                             (uint64_t)(int64_t)newfd);
}

#define SYSCALL_SOCKET_CREATE_NUM  130ULL
#define SYSCALL_SOCKET_CONNECT_NUM 131ULL
#define SYSCALL_SOCKET_BIND_NUM    132ULL
#define SYSCALL_SOCKET_LISTEN_NUM  133ULL
#define SYSCALL_SOCKET_ACCEPT_NUM  134ULL
#define SYSCALL_SOCKET_SEND_NUM    135ULL
#define SYSCALL_SOCKET_RECV_NUM    136ULL
#define SYSCALL_SOCKET_CLOSE_NUM   137ULL

int32_t socket_create(int32_t type)
{
    return (int32_t)syscall1(SYSCALL_SOCKET_CREATE_NUM, (uint64_t)(int64_t)type);
}

int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port)
{
    (void)sockfd;
    return (int32_t)syscall2(SYSCALL_SOCKET_CONNECT_NUM,
                             (uint64_t)ip, (uint64_t)port);
}

int32_t socket_bind(int32_t sockfd, uint16_t port)
{
    return (int32_t)syscall2(SYSCALL_SOCKET_BIND_NUM,
                             (uint64_t)(int64_t)sockfd, (uint64_t)port);
}

int32_t socket_listen(int32_t sockfd)
{
    return (int32_t)syscall1(SYSCALL_SOCKET_LISTEN_NUM, (uint64_t)(int64_t)sockfd);
}

int32_t socket_accept(int32_t sockfd)
{
    return (int32_t)syscall1(SYSCALL_SOCKET_ACCEPT_NUM, (uint64_t)(int64_t)sockfd);
}

int32_t socket_send(int32_t sockfd, const void *data, uint32_t len)
{
    return (int32_t)syscall3(SYSCALL_SOCKET_SEND_NUM,
                             (uint64_t)(int64_t)sockfd,
                             (uint64_t)(uintptr_t)data,
                             (uint64_t)len);
}

int32_t socket_recv(int32_t sockfd, void *buf, uint32_t buf_len)
{
    return (int32_t)syscall3(SYSCALL_SOCKET_RECV_NUM,
                             (uint64_t)(int64_t)sockfd,
                             (uint64_t)(uintptr_t)buf,
                             (uint64_t)buf_len);
}

int32_t socket_close(int32_t sockfd)
{
    return (int32_t)syscall1(SYSCALL_SOCKET_CLOSE_NUM, (uint64_t)(int64_t)sockfd);
}

#define SYSCALL_KVM_OPEN_NUM   240ULL
#define SYSCALL_KVM_IOCTL_NUM  241ULL
#define SYSCALL_KVM_CLOSE_NUM  242ULL
#define SYSCALL_KVM_MMAP_NUM   243ULL

int32_t kvm_open(void)
{
    return (int32_t)syscall0(SYSCALL_KVM_OPEN_NUM);
}

int64_t kvm_ioctl(int32_t fd, uint64_t request, uint64_t arg)
{
    return (int64_t)syscall3(SYSCALL_KVM_IOCTL_NUM,
                             (uint64_t)(int64_t)fd,
                             request,
                             arg);
}

int32_t kvm_close(int32_t fd)
{
    return (int32_t)syscall1(SYSCALL_KVM_CLOSE_NUM, (uint64_t)(int64_t)fd);
}

void *kvm_mmap(int32_t fd, uint64_t offset, uint64_t size)
{
    return (void *)(uintptr_t)syscall3(SYSCALL_KVM_MMAP_NUM,
                                       (uint64_t)(int64_t)fd,
                                       offset,
                                       size);
}
