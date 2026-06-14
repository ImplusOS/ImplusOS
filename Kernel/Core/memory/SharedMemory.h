#pragma once

#include <stdint.h>

int32_t shared_memory_create(uint32_t size);
int32_t shared_memory_grant(int32_t handle, int32_t pid);
void *shared_memory_map(int32_t handle);
int32_t shared_memory_unmap(int32_t handle, void *address);
int32_t shared_memory_close(int32_t handle);
void shared_memory_cleanup_process(int32_t pid);
