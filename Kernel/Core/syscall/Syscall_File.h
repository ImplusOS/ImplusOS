#pragma once

#include <stdint.h>
#include "Core/vfs/VFS.h"

void syscall_file_init(void);
int32_t syscall_file_open(const char *path, uint64_t flags);
int32_t syscall_file_creat(const char *path);
int64_t syscall_file_read(int32_t fd, uint8_t *buffer, uint64_t len);
int64_t syscall_file_write(int32_t fd, const uint8_t *buffer, uint64_t len);
int64_t syscall_file_seek(int32_t fd, int64_t offset, int32_t whence);
int32_t syscall_file_close(int32_t fd);
int32_t syscall_file_mkdir(const char *path);
int32_t syscall_file_opendir(const char *path);
int32_t syscall_file_readdir(int32_t dir_handle, vfs_dirent_t *out_entry);
int32_t syscall_file_closedir(int32_t dir_handle);
int32_t syscall_file_unlink(const char *path);
int32_t syscall_file_rename(const char *old_path, const char *new_path);
void syscall_file_close_all_for_pid(int32_t pid, uint32_t *closed_fds_out, uint32_t *closed_dirs_out);
int32_t syscall_file_pipe(int32_t fds_out[2]);
int32_t syscall_file_dup(int32_t oldfd);
int32_t syscall_file_dup2(int32_t oldfd, int32_t newfd);
int32_t syscall_file_dup_at_least(int32_t oldfd, int32_t minimum_fd);
int32_t syscall_file_truncate(int32_t fd, uint64_t length);
int32_t syscall_file_get_status_flags(int32_t fd);
int32_t syscall_file_set_status_flags(int32_t fd, uint32_t flags);
int32_t syscall_file_get_descriptor_flags(int32_t fd);
int32_t syscall_file_set_descriptor_flags(int32_t fd, uint32_t flags);
int64_t syscall_file_available(int32_t fd);
uint32_t syscall_file_poll(int32_t fd, uint32_t events);
int32_t syscall_file_register_dir(const char *path);
int32_t syscall_file_get_dir_dirent(int32_t fd, vfs_dirent_t *out_entry);
int32_t syscall_file_get_file_info(int32_t fd, vfs_file_t *file_out,
                                   uint32_t *writable_out);
int32_t syscall_file_create_timerfd(void);
int32_t syscall_file_timerfd_settime(int32_t fd,
                                     uint64_t it_value_sec,
                                     uint64_t it_value_nsec,
                                     uint64_t it_interval_sec,
                                     uint64_t it_interval_nsec);
int32_t syscall_file_timerfd_gettime(int32_t fd,
                                     uint64_t *it_value_sec_out,
                                     uint64_t *it_value_nsec_out,
                                     uint64_t *it_interval_sec_out,
                                     uint64_t *it_interval_nsec_out);
int32_t syscall_file_create_memfd(const char *name);
int32_t syscall_file_create_signalfd(uint64_t mask);
int32_t syscall_file_signalfd_set_mask(int32_t fd, uint64_t mask);
int64_t syscall_timerfd_read(int32_t fd, uint8_t *buffer, uint64_t len);
int64_t syscall_memfd_read(int32_t fd, uint8_t *buffer, uint64_t len);
int64_t syscall_memfd_write(int32_t fd, const uint8_t *buffer, uint64_t len);
int64_t syscall_signalfd_read(int32_t fd, uint8_t *buffer, uint64_t len);
