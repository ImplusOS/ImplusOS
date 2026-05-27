#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct vfs_driver;

typedef struct vfs_file {
    uint64_t internal_id;
    uint32_t size;
    void *driver_data;
    const struct vfs_driver *fs_driver;
} vfs_file_t;

typedef struct vfs_dirent {
    char name[256];
    bool is_directory;
    uint32_t size;
} vfs_dirent_t;

typedef struct vfs_driver {
    const char *fs_type;
    const char *prefix;
    bool (*find_file)(const char *path, vfs_file_t *out_file);
    bool (*read_file)(vfs_file_t *file, uint8_t *buffer);
    bool (*write_file)(vfs_file_t *file, const uint8_t *buffer);
    bool (*read_at)(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size);
    bool (*write_at)(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size);
    bool (*truncate)(vfs_file_t *file, uint32_t new_size);
    uint32_t (*get_file_size)(vfs_file_t *file);
    bool (*creat)(const char *path);
    bool (*mkdir)(const char *path);
    int32_t (*opendir)(const char *path);
    int32_t (*readdir)(int32_t handle, vfs_dirent_t *out_entry);
    int32_t (*closedir)(int32_t handle);
    bool (*unlink)(const char *path);
    void (*list_root)(void);
    void (*set_case_sensitive)(bool enabled);
    bool (*get_case_sensitive)(void);
} vfs_driver_t;