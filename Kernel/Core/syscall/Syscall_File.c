#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "Syscall_File.h"

#include "kernel/status.h"
#include "Core/vfs/VFS.h"
#include "kernel/config.h"
#include "Core/process/ProcessManager.h"
#include "Core/sync/Spinlock.h"
#include "Debug/serial/Serial.h"

enum {
    FILE_MAX_FD = FILE_MAX_FD_CONFIG,
    FILE_MAX_DIR_HANDLE = FILE_MAX_DIR_HANDLE_CONFIG
};

#define FILE_IO_CHUNK_SIZE   (64U * 1024U)
#define FILE_READ_CACHE_SIZE 4096U
#define FILE_SEEK_SET        0
#define FILE_SEEK_CUR        1
#define FILE_SEEK_END        2
#define FILE_O_ACCMODE       0x0003u
#define FILE_O_WRONLY        0x0001u
#define FILE_O_RDWR          0x0002u
#define FILE_O_APPEND        0x0400u
#define FILE_O_NONBLOCK      0x0800u
#define FILE_FD_CLOEXEC      0x0001u

#define PIPE_MAX_COUNT       16
#define PIPE_BUF_SIZE        4096u

typedef struct {
    uint8_t used;
    int32_t owner_pid;
    int32_t open_index;
    uint32_t status_flags;
    uint32_t descriptor_flags;
} kernel_file_t;

typedef struct {
    uint8_t used;
    uint8_t writable;
    vfs_file_t file;
    uint32_t offset;
    uint32_t refcount;
    uint8_t cache_valid;
    uint32_t cache_offset;
    uint32_t cache_size;
    uint8_t cache_data[FILE_READ_CACHE_SIZE];
} kernel_open_file_t;

typedef struct {
    uint8_t used;
    int32_t owner_pid;
    int32_t vfs_handle;
} kernel_dir_t;

typedef struct {
    uint8_t  in_use;
    uint8_t  data[PIPE_BUF_SIZE];
    uint16_t read_pos;
    uint16_t write_pos;
    uint16_t count;
    uint16_t reader_count;
    uint16_t writer_count;
    spinlock_t lock;
} kernel_pipe_t;

static kernel_file_t g_files[FILE_MAX_FD];
static kernel_open_file_t g_open_files[FILE_MAX_FD];
static kernel_dir_t g_dirs[FILE_MAX_DIR_HANDLE];
static kernel_pipe_t g_pipes[PIPE_MAX_COUNT];
static spinlock_t g_file_table_lock;
static spinlock_t g_dir_table_lock;

static kernel_pipe_t *find_pipe_for_fd(int32_t fd, int *is_read_end);
static int64_t syscall_pipe_read(int32_t fd, uint8_t *buffer, uint64_t len);
static int64_t syscall_pipe_write(int32_t fd, const uint8_t *buffer, uint64_t len);

__attribute__((unused))
static uint32_t count_used_file_slots(void)
{
    uint32_t used = 0;
    for (int32_t fd = 0; fd < FILE_MAX_FD; ++fd) {
        if (g_files[fd].used != 0) {
            ++used;
        }
    }
    return used;
}

__attribute__((unused))
static uint32_t count_used_dir_slots(void)
{
    uint32_t used = 0;
    for (int32_t i = 0; i < FILE_MAX_DIR_HANDLE; ++i) {
        if (g_dirs[i].used != 0) {
            ++used;
        }
    }
    return used;
}

static void open_file_cache_invalidate(kernel_open_file_t *file)
{
    if (file == NULL) {
        return;
    }

    file->cache_valid = 0;
    file->cache_offset = 0;
    file->cache_size = 0;
}

static int open_file_cache_refill(kernel_open_file_t *file, uint32_t offset)
{
    if (file == NULL) {
        return 0;
    }

    if (offset >= file->file.size) {
        open_file_cache_invalidate(file);
        return 1;
    }

    uint32_t remaining = file->file.size - offset;
    uint32_t to_cache = remaining;
    if (to_cache > FILE_READ_CACHE_SIZE) {
        to_cache = FILE_READ_CACHE_SIZE;
    }

    if (!vfs_read_at(&file->file, offset, file->cache_data, to_cache)) {
        open_file_cache_invalidate(file);
        return 0;
    }

    file->cache_valid = 1;
    file->cache_offset = offset;
    file->cache_size = to_cache;
    return 1;
}

static kernel_open_file_t *fd_open_file(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used != 1) {
        return NULL;
    }

    int32_t open_index = g_files[fd].open_index;
    if (open_index < 0 || open_index >= FILE_MAX_FD ||
        g_open_files[open_index].used == 0) {
        return NULL;
    }

    return &g_open_files[open_index];
}

static int32_t allocate_open_file_locked(void)
{
    for (int32_t i = 0; i < FILE_MAX_FD; ++i) {
        if (g_open_files[i].used == 0) {
            return i;
        }
    }
    return -1;
}

static void release_fd_locked(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0) {
        return;
    }

    if (g_files[fd].used == 1) {
        int32_t open_index = g_files[fd].open_index;
        if (open_index >= 0 && open_index < FILE_MAX_FD &&
            g_open_files[open_index].used != 0) {
            if (g_open_files[open_index].refcount > 0) {
                --g_open_files[open_index].refcount;
            }
            if (g_open_files[open_index].refcount == 0) {
                vfs_close_file(&g_open_files[open_index].file);
                memset(&g_open_files[open_index], 0, sizeof(g_open_files[open_index]));
            }
        }
    } else if (g_files[fd].used == 2 || g_files[fd].used == 3) {
        int32_t pipe_index = g_files[fd].open_index;
        if (pipe_index >= 0 && pipe_index < PIPE_MAX_COUNT &&
            g_pipes[pipe_index].in_use != 0u) {
            kernel_pipe_t *pipe = &g_pipes[pipe_index];
            if (g_files[fd].used == 2 && pipe->reader_count > 0u) {
                --pipe->reader_count;
            } else if (g_files[fd].used == 3 && pipe->writer_count > 0u) {
                --pipe->writer_count;
            }
            if (pipe->reader_count == 0u && pipe->writer_count == 0u) {
                memset(pipe, 0, sizeof(*pipe));
            }
        }
    }

    memset(&g_files[fd], 0, sizeof(g_files[fd]));
    g_files[fd].open_index = -1;
}

static int fd_is_owned_by_current_process(int32_t fd)
{
    int32_t current_pid = process_get_current_pid();
    if (current_pid < 0) {
        return 0;
    }
    return (g_files[fd].owner_pid == current_pid);
}

static int dir_is_owned_by_current_process(int32_t dir_handle)
{
    int32_t current_pid = process_get_current_pid();
    if (current_pid < 0) {
        return 0;
    }
    return (g_dirs[dir_handle].owner_pid == current_pid);
}

void syscall_file_init(void)
{
    spinlock_init(&g_file_table_lock);
    spinlock_init(&g_dir_table_lock);
    memset(g_files, 0, sizeof(g_files));
    memset(g_open_files, 0, sizeof(g_open_files));
    memset(g_dirs, 0, sizeof(g_dirs));
    memset(g_pipes, 0, sizeof(g_pipes));
    for (int32_t fd = 0; fd < FILE_MAX_FD; ++fd) {
        g_files[fd].open_index = -1;
    }
}

int32_t syscall_file_open(const char *path, uint64_t flags)
{
    vfs_file_t file;
    int32_t current_pid = process_get_current_pid();

    if (path == NULL || path[0] == '\0' || current_pid < 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }

    if (!vfs_find_file(path, &file)) {
        return (int32_t)OS_STATUS_NOT_FOUND;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);
    for (int32_t fd = 0; fd < FILE_MAX_FD; ++fd) {
        if (g_files[fd].used == 0) {
            int32_t open_index = allocate_open_file_locked();
            if (open_index < 0) {
                spinlock_unlock(&g_file_table_lock);
                irq_restore(irq_flags);
                return (int32_t)OS_STATUS_LIMIT_REACHED;
            }

            memset(&g_open_files[open_index], 0, sizeof(g_open_files[open_index]));
            g_open_files[open_index].used = 1;
            uint32_t access_mode = (uint32_t)flags & FILE_O_ACCMODE;
            g_open_files[open_index].writable =
                (access_mode == FILE_O_WRONLY || access_mode == FILE_O_RDWR) ? 1u : 0u;
            g_open_files[open_index].file = file;
            g_open_files[open_index].offset = 0;
            g_open_files[open_index].refcount = 1;
            open_file_cache_invalidate(&g_open_files[open_index]);

            g_files[fd].used = 1;
            g_files[fd].owner_pid = current_pid;
            g_files[fd].open_index = open_index;
            g_files[fd].status_flags = (uint32_t)flags;
            spinlock_unlock(&g_file_table_lock);
            irq_restore(irq_flags);
            return fd;
        }
    }
    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);
    return (int32_t)OS_STATUS_LIMIT_REACHED;
}

int32_t syscall_file_creat(const char *path)
{
    if (path == NULL || path[0] == '\0' || process_get_current_pid() < 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    if (!vfs_creat(path)) {
        return (int32_t)OS_STATUS_IO_ERROR;
    }
    return syscall_file_open(path, 1ULL);
}

int64_t syscall_file_read(int32_t fd, uint8_t *buffer, uint64_t len)
{
    if (fd < 0 || fd >= FILE_MAX_FD || buffer == NULL || g_files[fd].used == 0) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }
    if (!fd_is_owned_by_current_process(fd)) {
        return (int64_t)OS_STATUS_ACCESS_DENIED;
    }
    if (len == 0) {
        return 0;
    }
    if (g_files[fd].used == 2) {
        return syscall_pipe_read(fd, buffer, len);
    }
    if (g_files[fd].used != 1) {
        return (int64_t)OS_STATUS_ACCESS_DENIED;
    }

    kernel_open_file_t *file = fd_open_file(fd);
    if (file == NULL) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }

    if (file->offset >= file->file.size) {
        return 0;
    }

    uint64_t remaining = (uint64_t)file->file.size - (uint64_t)file->offset;
    uint64_t to_read = (len < remaining) ? len : remaining;
    uint64_t read_total = 0;
    uint32_t cursor = file->offset;

    while (read_total < to_read) {
        uint32_t cache_end = file->cache_offset + file->cache_size;
        if (file->cache_valid == 0 || cursor < file->cache_offset || cursor >= cache_end) {
            if (!open_file_cache_refill(file, cursor)) {
                return (int64_t)OS_STATUS_IO_ERROR;
            }
            if (file->cache_size == 0) {
                break;
            }
            cache_end = file->cache_offset + file->cache_size;
        }

        uint32_t cache_index = cursor - file->cache_offset;
        uint32_t available = cache_end - cursor;
        uint64_t remaining_request = to_read - read_total;
        uint32_t chunk = available;
        if (remaining_request < (uint64_t)chunk) {
            chunk = (uint32_t)remaining_request;
        }

        memcpy(buffer + (size_t)read_total,
               file->cache_data + cache_index,
               (size_t)chunk);

        read_total += (uint64_t)chunk;
        cursor += chunk;
    }

    file->offset = cursor;
    return (int64_t)read_total;
}

int64_t syscall_file_write(int32_t fd, const uint8_t *buffer, uint64_t len)
{
    if (fd < 0 || fd >= FILE_MAX_FD || buffer == NULL || g_files[fd].used == 0) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }
    if (!fd_is_owned_by_current_process(fd)) {
        return (int64_t)OS_STATUS_ACCESS_DENIED;
    }
    if (g_files[fd].used == 3) {
        return syscall_pipe_write(fd, buffer, len);
    }
    if (g_files[fd].used != 1) {
        return (int64_t)OS_STATUS_ACCESS_DENIED;
    }

    kernel_open_file_t *file = fd_open_file(fd);
    if (file == NULL) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }

    if (file->writable == 0) {
        return (int64_t)OS_STATUS_ACCESS_DENIED;
    }
    if (len == 0) {
        return 0;
    }
    if ((g_files[fd].status_flags & FILE_O_APPEND) != 0u) {
        file->offset = file->file.size;
    }

    uint64_t write_total = 0;

    while (write_total < len) {
        uint64_t chunk64 = len - write_total;
        if (chunk64 > FILE_IO_CHUNK_SIZE) {
            chunk64 = FILE_IO_CHUNK_SIZE;
        }

        uint32_t chunk = (uint32_t)chunk64;
        uint32_t write_offset = file->offset + (uint32_t)write_total;
        if (!vfs_write_at(&file->file,
                            write_offset,
                            buffer + (size_t)write_total,
                            chunk)) {
            return (int64_t)OS_STATUS_IO_ERROR;
        }

        write_total += (uint64_t)chunk;
    }

    file->offset += (uint32_t)len;
    open_file_cache_invalidate(file);
    return (int64_t)len;
}

int64_t syscall_file_seek(int32_t fd, int64_t offset, int32_t whence)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }
    if (!fd_is_owned_by_current_process(fd)) {
        return (int64_t)OS_STATUS_ACCESS_DENIED;
    }

    kernel_open_file_t *file = fd_open_file(fd);
    if (file == NULL) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case FILE_SEEK_SET:
            base = 0;
            break;
        case FILE_SEEK_CUR:
            base = (int64_t)file->offset;
            break;
        case FILE_SEEK_END:
            base = (int64_t)file->file.size;
            break;
        default:
            return (int64_t)OS_STATUS_INVALID_ARG;
    }

    int64_t next = base + offset;
    if (next < 0 || (uint64_t)next > (uint64_t)file->file.size) {
        return (int64_t)OS_STATUS_INVALID_ARG;
    }

    file->offset = (uint32_t)next;
    return next;
}

int32_t syscall_file_close(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    if (!fd_is_owned_by_current_process(fd)) {
        return (int32_t)OS_STATUS_ACCESS_DENIED;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);
    release_fd_locked(fd);
    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int32_t syscall_file_mkdir(const char *path)
{
    if (path == NULL || path[0] == '\0' || process_get_current_pid() < 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }

    return vfs_mkdir(path) ? 0 : (int32_t)OS_STATUS_IO_ERROR;
}

int32_t syscall_file_opendir(const char *path)
{
    if (path == NULL || path[0] == '\0' || process_get_current_pid() < 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }

    int32_t vfs_handle = vfs_opendir(path);
    if (vfs_handle < 0) {
        return (int32_t)OS_STATUS_NOT_FOUND;
    }

    int32_t current_pid = process_get_current_pid();
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_dir_table_lock);
    for (int32_t i = 0; i < FILE_MAX_DIR_HANDLE; ++i) {
        if (g_dirs[i].used == 0) {
            g_dirs[i].used = 1;
            g_dirs[i].owner_pid = current_pid;
            g_dirs[i].vfs_handle = vfs_handle;
            spinlock_unlock(&g_dir_table_lock);
            irq_restore(irq_flags);
            return i;
        }
    }
    spinlock_unlock(&g_dir_table_lock);
    irq_restore(irq_flags);

    (void)vfs_closedir(vfs_handle);
    return (int32_t)OS_STATUS_LIMIT_REACHED;
}

int32_t syscall_file_readdir(int32_t dir_handle, vfs_dirent_t *out_entry)
{
    if (dir_handle < 0 || dir_handle >= FILE_MAX_DIR_HANDLE || out_entry == NULL ||
        g_dirs[dir_handle].used == 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    if (!dir_is_owned_by_current_process(dir_handle)) {
        return (int32_t)OS_STATUS_ACCESS_DENIED;
    }

    return vfs_readdir(g_dirs[dir_handle].vfs_handle, out_entry);
}

int32_t syscall_file_closedir(int32_t dir_handle)
{
    if (dir_handle < 0 || dir_handle >= FILE_MAX_DIR_HANDLE || g_dirs[dir_handle].used == 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    if (!dir_is_owned_by_current_process(dir_handle)) {
        return (int32_t)OS_STATUS_ACCESS_DENIED;
    }

    (void)vfs_closedir(g_dirs[dir_handle].vfs_handle);
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_dir_table_lock);
    memset(&g_dirs[dir_handle], 0, sizeof(g_dirs[dir_handle]));
    spinlock_unlock(&g_dir_table_lock);
    irq_restore(irq_flags);
    return 0;
}

int32_t syscall_file_unlink(const char *path)
{
    if (path == NULL || path[0] == '\0' || process_get_current_pid() < 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    return vfs_unlink(path) ? 0 : (int32_t)OS_STATUS_IO_ERROR;
}

int32_t syscall_file_rename(const char *old_path, const char *new_path)
{
    if (old_path == NULL || new_path == NULL ||
        old_path[0] == '\0' || new_path[0] == '\0' ||
        process_get_current_pid() < 0) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    return vfs_rename(old_path, new_path) ?
        0 : (int32_t)OS_STATUS_IO_ERROR;
}

void syscall_file_close_all_for_pid(int32_t pid, uint32_t *closed_fds_out, uint32_t *closed_dirs_out)
{
    if (closed_fds_out != NULL) {
        *closed_fds_out = 0;
    }
    if (closed_dirs_out != NULL) {
        *closed_dirs_out = 0;
    }

    if (pid < 0) {
        return;
    }

    uint32_t closed_fds = 0;
    uint32_t closed_dirs = 0;

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);
    for (int32_t fd = 0; fd < FILE_MAX_FD; ++fd) {
        if (g_files[fd].used != 0 && g_files[fd].owner_pid == pid) {
            release_fd_locked(fd);
            ++closed_fds;
        }
    }
    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);

    irq_flags = irq_save_disable();
    spinlock_lock(&g_dir_table_lock);
    for (int32_t i = 0; i < FILE_MAX_DIR_HANDLE; ++i) {
        if (g_dirs[i].used != 0 && g_dirs[i].owner_pid == pid) {
            (void)vfs_closedir(g_dirs[i].vfs_handle);
            memset(&g_dirs[i], 0, sizeof(g_dirs[i]));
            ++closed_dirs;
        }
    }
    spinlock_unlock(&g_dir_table_lock);
    irq_restore(irq_flags);

    if (closed_fds_out != NULL) {
        *closed_fds_out = closed_fds;
    }
    if (closed_dirs_out != NULL) {
        *closed_dirs_out = closed_dirs;
    }
}

int32_t syscall_file_pipe(int32_t fds_out[2])
{
    if (fds_out == NULL) {
        return (int32_t)OS_STATUS_FAULT;
    }

    int32_t pipe_idx = -1;
    for (int32_t i = 0; i < PIPE_MAX_COUNT; ++i) {
        if (g_pipes[i].in_use == 0) {
            pipe_idx = i;
            break;
        }
    }
    if (pipe_idx < 0) {
        return (int32_t)OS_STATUS_LIMIT_REACHED;
    }

    int32_t read_fd = -1, write_fd = -1;
    int32_t current_pid = process_get_current_pid();

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);
    for (int32_t fd = 0; fd < FILE_MAX_FD && (read_fd < 0 || write_fd < 0); ++fd) {
        if (g_files[fd].used == 0) {
            if (read_fd < 0) {
                read_fd = fd;
            } else {
                write_fd = fd;
            }
        }
    }

    if (read_fd < 0 || write_fd < 0) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_LIMIT_REACHED;
    }

    memset(&g_files[read_fd], 0, sizeof(g_files[read_fd]));
    g_files[read_fd].used = 2;
    g_files[read_fd].owner_pid = current_pid;
    g_files[read_fd].open_index = pipe_idx;
    g_files[read_fd].status_flags = 0u;

    memset(&g_files[write_fd], 0, sizeof(g_files[write_fd]));
    g_files[write_fd].used = 3;
    g_files[write_fd].owner_pid = current_pid;
    g_files[write_fd].open_index = pipe_idx;
    g_files[write_fd].status_flags = FILE_O_WRONLY;

    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);

    kernel_pipe_t *pipe = &g_pipes[pipe_idx];
    memset(pipe, 0, sizeof(*pipe));
    spinlock_init(&pipe->lock);
    pipe->in_use = 1;
    pipe->reader_count = 1u;
    pipe->writer_count = 1u;

    fds_out[0] = read_fd;
    fds_out[1] = write_fd;

    return 0;
}

static kernel_pipe_t *find_pipe_for_fd(int32_t fd, int *is_read_end)
{
    if (fd < 0 || fd >= FILE_MAX_FD ||
        (g_files[fd].used != 2 && g_files[fd].used != 3)) {
        return NULL;
    }
    int32_t index = g_files[fd].open_index;
    if (index < 0 || index >= PIPE_MAX_COUNT || g_pipes[index].in_use == 0u) {
        return NULL;
    }
    if (is_read_end) *is_read_end = (g_files[fd].used == 2);
    return &g_pipes[index];
}

static int64_t syscall_pipe_read(int32_t fd, uint8_t *buffer, uint64_t len)
{
    int is_read = 0;
    kernel_pipe_t *pipe = find_pipe_for_fd(fd, &is_read);
    if (pipe == NULL || !is_read) return (int64_t)OS_STATUS_IO_ERROR;

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&pipe->lock);

    uint64_t bytes_read = 0;
    while (bytes_read < len && pipe->count > 0) {
        buffer[bytes_read++] = pipe->data[pipe->read_pos];
        pipe->read_pos = (uint16_t)((pipe->read_pos + 1u) % PIPE_BUF_SIZE);
        pipe->count--;
    }

    spinlock_unlock(&pipe->lock);
    irq_restore(irq_flags);

    return (int64_t)bytes_read;
}

static int64_t syscall_pipe_write(int32_t fd, const uint8_t *buffer, uint64_t len)
{
    int is_read = 0;
    kernel_pipe_t *pipe = find_pipe_for_fd(fd, &is_read);
    if (pipe == NULL || is_read) return (int64_t)OS_STATUS_IO_ERROR;

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&pipe->lock);

    uint64_t bytes_written = 0;
    while (bytes_written < len && pipe->count < PIPE_BUF_SIZE) {
        pipe->data[pipe->write_pos] = buffer[bytes_written++];
        pipe->write_pos = (uint16_t)((pipe->write_pos + 1u) % PIPE_BUF_SIZE);
        pipe->count++;
    }

    spinlock_unlock(&pipe->lock);
    irq_restore(irq_flags);

    return (int64_t)bytes_written;
}

int syscall_file_is_pipe(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD) return 0;
    return (g_files[fd].used == 2 || g_files[fd].used == 3);
}

uint32_t syscall_file_poll(int32_t fd, uint32_t events)
{
    enum {
        POLL_IN = 0x0001u,
        POLL_OUT = 0x0004u,
        POLL_ERROR = 0x0008u,
        POLL_INVALID = 0x0020u
    };
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0 ||
        !fd_is_owned_by_current_process(fd))
        return POLL_INVALID;

    if (g_files[fd].used == 1) {
        kernel_open_file_t *file = fd_open_file(fd);
        if (file == NULL) return POLL_ERROR;
        uint32_t ready = 0u;
        if ((events & POLL_IN) != 0u) ready |= POLL_IN;
        if ((events & POLL_OUT) != 0u && file->writable != 0u)
            ready |= POLL_OUT;
        return ready;
    }

    int is_read_end = 0;
    kernel_pipe_t *pipe = find_pipe_for_fd(fd, &is_read_end);
    if (pipe == NULL) return POLL_ERROR;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&pipe->lock);
    uint32_t ready = 0u;
    if (is_read_end && (events & POLL_IN) != 0u && pipe->count != 0u)
        ready |= POLL_IN;
    if (!is_read_end && (events & POLL_OUT) != 0u && pipe->count < PIPE_BUF_SIZE)
        ready |= POLL_OUT;
    spinlock_unlock(&pipe->lock);
    irq_restore(irq_flags);
    return ready;
}

int32_t syscall_file_dup(int32_t oldfd)
{
    if (oldfd < 0 || oldfd >= FILE_MAX_FD) {
        return (int32_t)OS_STATUS_FAULT;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);

    if (g_files[oldfd].used == 0 || !fd_is_owned_by_current_process(oldfd)) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_FAULT;
    }

    kernel_open_file_t *open_file = NULL;
    if (g_files[oldfd].used == 1) {
        open_file = fd_open_file(oldfd);
    }
    if (g_files[oldfd].used == 1 && open_file == NULL) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_FAULT;
    }

    int32_t newfd = -1;
    for (int32_t fd = 0; fd < FILE_MAX_FD; ++fd) {
        if (g_files[fd].used == 0) {
            newfd = fd;
            break;
        }
    }

    if (newfd < 0) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_LIMIT_REACHED;
    }

    memcpy(&g_files[newfd], &g_files[oldfd], sizeof(g_files[newfd]));
    if (open_file != NULL) {
        open_file->refcount++;
    } else {
        kernel_pipe_t *pipe = find_pipe_for_fd(newfd, NULL);
        if (pipe != NULL) {
            if (g_files[newfd].used == 2) ++pipe->reader_count;
            else ++pipe->writer_count;
        }
    }

    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);

    return newfd;
}

int32_t syscall_file_dup2(int32_t oldfd, int32_t newfd)
{
    if (oldfd < 0 || oldfd >= FILE_MAX_FD || newfd < 0 || newfd >= FILE_MAX_FD) {
        return (int32_t)OS_STATUS_FAULT;
    }
    if (oldfd == newfd) return newfd;

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);

    if (g_files[oldfd].used == 0 || !fd_is_owned_by_current_process(oldfd)) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_FAULT;
    }

    kernel_open_file_t *open_file = NULL;
    if (g_files[oldfd].used == 1) {
        open_file = fd_open_file(oldfd);
    }
    if (g_files[oldfd].used == 1 && open_file == NULL) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_FAULT;
    }
    
    if (g_files[newfd].used != 0) {
        if (g_files[newfd].owner_pid != process_get_current_pid()) {
            spinlock_unlock(&g_file_table_lock);
            irq_restore(irq_flags);
            return (int32_t)OS_STATUS_ACCESS_DENIED;
        }
        release_fd_locked(newfd);
    }

    memcpy(&g_files[newfd], &g_files[oldfd], sizeof(g_files[newfd]));
    if (open_file != NULL) {
        open_file->refcount++;
    } else {
        kernel_pipe_t *pipe = find_pipe_for_fd(newfd, NULL);
        if (pipe != NULL) {
            if (g_files[newfd].used == 2) ++pipe->reader_count;
            else ++pipe->writer_count;
        }
    }

    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);

    return newfd;
}

int32_t syscall_file_dup_at_least(int32_t oldfd, int32_t minimum_fd)
{
    if (oldfd < 0 || oldfd >= FILE_MAX_FD ||
        minimum_fd < 0 || minimum_fd >= FILE_MAX_FD) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_file_table_lock);
    if (g_files[oldfd].used == 0 || !fd_is_owned_by_current_process(oldfd)) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_FAULT;
    }

    int32_t newfd = -1;
    for (int32_t fd = minimum_fd; fd < FILE_MAX_FD; ++fd) {
        if (g_files[fd].used == 0) {
            newfd = fd;
            break;
        }
    }
    if (newfd < 0) {
        spinlock_unlock(&g_file_table_lock);
        irq_restore(irq_flags);
        return (int32_t)OS_STATUS_LIMIT_REACHED;
    }

    memcpy(&g_files[newfd], &g_files[oldfd], sizeof(g_files[newfd]));
    if (g_files[newfd].used == 1) {
        kernel_open_file_t *open_file = fd_open_file(newfd);
        if (open_file == NULL) {
            memset(&g_files[newfd], 0, sizeof(g_files[newfd]));
            g_files[newfd].open_index = -1;
            spinlock_unlock(&g_file_table_lock);
            irq_restore(irq_flags);
            return (int32_t)OS_STATUS_FAULT;
        }
        ++open_file->refcount;
    } else {
        kernel_pipe_t *pipe = find_pipe_for_fd(newfd, NULL);
        if (pipe != NULL) {
            if (g_files[newfd].used == 2) ++pipe->reader_count;
            else ++pipe->writer_count;
        }
    }

    spinlock_unlock(&g_file_table_lock);
    irq_restore(irq_flags);
    return newfd;
}

int32_t syscall_file_truncate(int32_t fd, uint64_t length)
{
    if (length > UINT32_MAX || fd < 0 || fd >= FILE_MAX_FD ||
        g_files[fd].used != 1 || !fd_is_owned_by_current_process(fd)) {
        return (int32_t)OS_STATUS_INVALID_ARG;
    }
    kernel_open_file_t *file = fd_open_file(fd);
    if (file == NULL || file->writable == 0u) {
        return (int32_t)OS_STATUS_ACCESS_DENIED;
    }
    if (!vfs_truncate(&file->file, (uint32_t)length)) {
        return (int32_t)OS_STATUS_IO_ERROR;
    }
    file->file.size = (uint32_t)length;
    if (file->offset > file->file.size) file->offset = file->file.size;
    open_file_cache_invalidate(file);
    return 0;
}

int32_t syscall_file_get_status_flags(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0 ||
        !fd_is_owned_by_current_process(fd)) {
        return (int32_t)OS_STATUS_FAULT;
    }
    return (int32_t)g_files[fd].status_flags;
}

int32_t syscall_file_set_status_flags(int32_t fd, uint32_t flags)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0 ||
        !fd_is_owned_by_current_process(fd)) {
        return (int32_t)OS_STATUS_FAULT;
    }
    uint32_t preserved = g_files[fd].status_flags & FILE_O_ACCMODE;
    g_files[fd].status_flags =
        preserved | (flags & (FILE_O_APPEND | FILE_O_NONBLOCK));
    return 0;
}

int32_t syscall_file_get_descriptor_flags(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0 ||
        !fd_is_owned_by_current_process(fd)) {
        return (int32_t)OS_STATUS_FAULT;
    }
    return (int32_t)g_files[fd].descriptor_flags;
}

int32_t syscall_file_set_descriptor_flags(int32_t fd, uint32_t flags)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0 ||
        !fd_is_owned_by_current_process(fd)) {
        return (int32_t)OS_STATUS_FAULT;
    }
    g_files[fd].descriptor_flags = flags & FILE_FD_CLOEXEC;
    return 0;
}

int64_t syscall_file_available(int32_t fd)
{
    if (fd < 0 || fd >= FILE_MAX_FD || g_files[fd].used == 0 ||
        !fd_is_owned_by_current_process(fd)) {
        return (int64_t)OS_STATUS_FAULT;
    }
    if (g_files[fd].used == 1) {
        kernel_open_file_t *file = fd_open_file(fd);
        if (file == NULL) return (int64_t)OS_STATUS_FAULT;
        return file->offset < file->file.size ?
            (int64_t)(file->file.size - file->offset) : 0;
    }
    int is_read_end = 0;
    kernel_pipe_t *pipe = find_pipe_for_fd(fd, &is_read_end);
    if (pipe == NULL || !is_read_end) return 0;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&pipe->lock);
    int64_t available = (int64_t)pipe->count;
    spinlock_unlock(&pipe->lock);
    irq_restore(irq_flags);
    return available;
}
