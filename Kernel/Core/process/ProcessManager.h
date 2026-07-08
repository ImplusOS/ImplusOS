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
#define PROCESS_CAP_DISPLAY (1ULL << 8)

#define PROCESS_CAP_DEFAULT_MASK \
    (PROCESS_CAP_SERIAL | PROCESS_CAP_PROCESS | PROCESS_CAP_FILE | PROCESS_CAP_MEMORY | PROCESS_CAP_INPUT | PROCESS_CAP_SIGNAL | PROCESS_CAP_IPC | PROCESS_CAP_NETWORK | PROCESS_CAP_DISPLAY)

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    uint8_t state;
    uint8_t reserved[7];
    char name[64];
    uint64_t runtime_ns;
    uint64_t ready_wait_ns;
    uint64_t max_ready_wait_ns;
    uint64_t context_switches;
    uint64_t voluntary_switches;
    uint64_t involuntary_switches;
    uint64_t syscalls;
    uint64_t ipc_send;
    uint64_t ipc_recv;
    uint64_t block_count;
    uint64_t wake_count;
    uint64_t blocked_ns;
    uint64_t display_present_calls;
    uint64_t display_rects;
    uint64_t display_bytes;
    uint64_t memory_usage;
} process_perf_info_t;

#define PROCESS_SCHEDULE_REQUEST_SWITCH      1
#define PROCESS_SCHEDULE_REQUEST_INVOLUNTARY 2

void process_manager_init(void);
int32_t process_register_boot_process(const char *path, uint64_t *entry_out);
int32_t process_create_user(uint64_t entry);
int32_t process_create_user_ex(uint64_t entry,
                               uint64_t arg1,
                               uint64_t arg2,
                               uint64_t arg3,
                               uint64_t arg4);
int32_t process_create_thread(uint64_t entry,
                              uint64_t arg1,
                              uint64_t arg2,
                              uint64_t arg3,
                              uint64_t arg4);
int32_t process_spawn_user_elf(const char *path);
int32_t process_spawn_user_elf_with_arg(const char *path,
                                        const char *launch_argument);
int32_t process_fork(void);
int32_t process_execve(const char *path, const char *const *argv,
                       const char *const *envp);
int32_t process_copy_launch_argument(char *out, uint32_t capacity);
void process_exit_current_with_status(int32_t exit_status);
void process_exit_current(void);
void process_thread_exit_current(int32_t exit_status);
int32_t process_get_current_pid(void);
int32_t process_get_current_tid(void);
int process_thread_join(int32_t tid);
int process_thread_detach(int32_t tid);
void process_set_current_fs_base(uint64_t fs_base);
uint64_t process_get_current_fs_base(void);
int process_set_clear_child_tid(uint64_t address);
int process_set_robust_list(uint64_t head, uint64_t length);
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
int process_user_munmap(void *ptr, uint64_t length);
uint64_t process_signal_set_handler(int32_t signum, uint64_t handler);
uint64_t process_signal_get_handler(int32_t signum);
uint64_t process_signal_get_mask(void);
int process_signal_set_mask(uint64_t mask);
int process_signal_deliver(int32_t pid, int32_t signum);
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
int process_run_next_on_current_cpu(void);
int32_t current_pid_get(void);
int process_is_alive(int32_t pid);
int process_sleep_current_ms(uint64_t ms);
int process_block_current(void);
int process_wake_pid(int32_t pid);
int32_t process_terminate(int32_t pid);
int32_t process_get_full_info(int32_t pid, void *info_out);
int32_t process_get_perf_info(int32_t pid, process_perf_info_t *info_out);
int32_t process_get_capacity(void);
void process_perf_note_syscall(int32_t pid);
void process_perf_note_ipc_send(int32_t pid);
void process_perf_note_ipc_recv(int32_t pid);
void process_perf_note_display(int32_t pid, uint32_t rect_count, uint64_t bytes);
void process_debug_dump_summary(const char *reason);
void process_debug_dump_cpu_usage(const char *reason, uint64_t interval_ns);
