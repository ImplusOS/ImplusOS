#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "Drivers/Module/DriverManager.h"

typedef FAT32_FILE VFS_FILE;
typedef FAT32_DIRENT VFS_DIRENT;

typedef struct {
    const char *prefix;
    bool (*find_file)(const char *path, VFS_FILE *out_file);
    bool (*read_file)(VFS_FILE *file, uint8_t *buffer);
    bool (*write_file)(VFS_FILE *file, const uint8_t *buffer);
    bool (*read_at)(VFS_FILE *file, uint32_t offset, uint8_t *buffer, uint32_t size);
    bool (*write_at)(VFS_FILE *file, uint32_t offset, const uint8_t *buffer, uint32_t size);
    bool (*truncate)(VFS_FILE *file, uint32_t new_size);
    uint32_t (*get_file_size)(VFS_FILE *file);
    bool (*creat)(const char *path);
    bool (*mkdir)(const char *path);
    int32_t (*opendir)(const char *path);
    int32_t (*readdir)(int32_t handle, VFS_DIRENT *out_entry);
    int32_t (*closedir)(int32_t handle);
    bool (*unlink)(const char *path);
} vfs_driver_t;

bool vfs_init(void);
void vfs_mount(const char *prefix, const vfs_driver_t *driver);
bool vfs_find_file(const char *path, VFS_FILE *out_file);
bool vfs_read_file(VFS_FILE *file, uint8_t *buffer);
bool vfs_write_file(VFS_FILE *file, const uint8_t *buffer);
bool vfs_read_at(VFS_FILE *file, uint32_t offset, uint8_t *buffer, uint32_t size);
bool vfs_write_at(VFS_FILE *file, uint32_t offset, const uint8_t *buffer, uint32_t size);
bool vfs_truncate(VFS_FILE *file, uint32_t new_size);
uint32_t vfs_get_file_size(VFS_FILE *file);
void vfs_list_root(void);
bool vfs_creat(const char *path);
bool vfs_mkdir(const char *path);
int32_t vfs_opendir(const char *path);
int32_t vfs_readdir(int32_t handle, VFS_DIRENT *out_entry);
int32_t vfs_closedir(int32_t handle);
bool vfs_unlink(const char *path);
void vfs_set_case_sensitive(bool enabled);
bool vfs_get_case_sensitive(void);
