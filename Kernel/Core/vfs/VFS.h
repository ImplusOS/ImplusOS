#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "kernel/interfaces/vfs_types.h"

bool vfs_init(void);
void vfs_mount(const char *prefix, const vfs_driver_t *driver);
bool vfs_set_default_fs_for_file(const char *path,
                                 uint32_t min_size,
                                 const uint8_t *magic,
                                 uint32_t magic_size);
bool vfs_find_file(const char *path, vfs_file_t *out_file);
bool vfs_read_file(vfs_file_t *file, uint8_t *buffer);
bool vfs_write_file(vfs_file_t *file, const uint8_t *buffer);
bool vfs_read_at(vfs_file_t *file, uint32_t offset, uint8_t *buffer, uint32_t size);
bool vfs_write_at(vfs_file_t *file, uint32_t offset, const uint8_t *buffer, uint32_t size);
bool vfs_truncate(vfs_file_t *file, uint32_t new_size);
uint32_t vfs_get_file_size(vfs_file_t *file);
bool vfs_close_file(vfs_file_t *file);
void vfs_list_root(void);
bool vfs_creat(const char *path);
bool vfs_mkdir(const char *path);
int32_t vfs_opendir(const char *path);
int32_t vfs_readdir(int32_t handle, vfs_dirent_t *out_entry);
int32_t vfs_closedir(int32_t handle);
bool vfs_unlink(const char *path);
bool vfs_rename(const char *old_path, const char *new_path);
void vfs_set_case_sensitive(bool enabled);
bool vfs_get_case_sensitive(void);
bool vfs_set_default_fs(const char *fs_type);
int vfs_driver_count_get(void);
