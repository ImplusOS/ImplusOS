#pragma once

#include <stdint.h>
#include "kernel/config.h"

#define USER_CODE_BASE    0x0000004000000000ULL
#define USER_CODE_LIMIT   0x0000004080000000ULL
#define USER_HEAP_BASE    0x0000004100000000ULL
#define USER_STACK_SIZE   (32 * 1024 * 1024ULL)
#define USER_STACK_TOP    0x0000004800000000ULL
#define USER_STACK_BASE   (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_HEAP_LIMIT   USER_STACK_BASE

#if (USER_CODE_BASE >= USER_CODE_LIMIT)
#error "Invalid user code range"
#endif

#if (USER_HEAP_BASE >= USER_HEAP_LIMIT)
#error "Invalid user heap range"
#endif

#if (USER_STACK_BASE >= USER_STACK_TOP)
#error "Invalid user stack range"
#endif

typedef uint64_t process_capability_mask_t;

#define PROCESS_CAP_SERIAL  (1ULL << 0)
#define PROCESS_CAP_PROCESS (1ULL << 1)
#define PROCESS_CAP_FILE    (1ULL << 2)
#define PROCESS_CAP_MEMORY  (1ULL << 3)
#define PROCESS_CAP_INPUT   (1ULL << 4)
#define PROCESS_CAP_SIGNAL  (1ULL << 5)
#define PROCESS_CAP_IPC     (1ULL << 6)
#define PROCESS_CAP_NETWORK (1ULL << 7)

#define PROCESS_CAP_DEFAULT_MASK \
    (PROCESS_CAP_SERIAL | PROCESS_CAP_PROCESS | PROCESS_CAP_FILE | PROCESS_CAP_MEMORY | PROCESS_CAP_INPUT | PROCESS_CAP_SIGNAL | PROCESS_CAP_IPC | PROCESS_CAP_NETWORK)

void process_manager_init(void);
int32_t process_register_boot_process(const char *path, uint64_t *entry_out);
int32_t process_create_user(uint64_t entry);
int32_t process_create_user_ex(uint64_t entry,
                               uint64_t arg1,
                               uint64_t arg2,
                               uint64_t arg3,
                               uint64_t arg4);
int32_t process_spawn_user_elf(const char *path);
void process_exit_current_with_status(int32_t exit_status);
void process_exit_current(void);
int32_t process_get_current_pid(void);
void process_set_current_fs_base(uint64_t fs_base);
uint64_t process_get_current_fs_base(void);
uint64_t process_get_current_saved_rsp(void);
uint64_t process_get_current_user_rsp(void);
uint64_t process_get_current_cr3(void);
uint64_t process_schedule_on_syscall(uint64_t current_saved_rsp,
                                     uint64_t current_user_rsp,
                                     int request_switch,
                                     uint64_t *next_user_rsp_out);
uint64_t process_schedule_after_exit(uint64_t *next_user_rsp_out);
int process_user_buffer_is_valid(const void *ptr, uint64_t len);
int process_user_cstring_length(const char *str, uint64_t max_len, uint64_t *len_out);
void *process_user_alloc(uint32_t size);
int process_user_free(void *ptr);
void *process_user_mmap(uint64_t length, uint64_t flags);
uint64_t process_signal_set_handler(int32_t signum, uint64_t handler);
int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options);
int32_t process_getppid(void);
int32_t process_get_parent_pid(int32_t pid);
int process_is_guard_page_fault(uint64_t fault_addr);
process_capability_mask_t process_default_capabilities(void);
process_capability_mask_t process_get_current_capabilities(void);
int process_current_has_capability(process_capability_mask_t capability);
int process_set_capabilities(int32_t pid, process_capability_mask_t capabilities);
int process_get_capabilities(int32_t pid, process_capability_mask_t *capabilities_out);
void process_on_timer_tick(void);
int  process_timeslice_expired(void);
int32_t current_pid_get(void);
int process_is_alive(int32_t pid);
