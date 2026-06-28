#pragma once

#include <stdint.h>
#include "kernel/config.h"
#include "ProcessManager.h"

#define PROCESS_KERNEL_STACK_SIZE (32 * 1024)
#define PROCESS_USER_ALLOC_MAX 4096
#define PROCESS_SIGNAL_MAX 32
#define PROCESS_FPU_STATE_SIZE 544U

#define PROCESS_STATE_UNUSED  0
#define PROCESS_STATE_READY   1
#define PROCESS_STATE_RUNNING 2
#define PROCESS_STATE_DEAD    3
#define PROCESS_STATE_INIT    4
#define PROCESS_STATE_ZOMBIE  5
#define PROCESS_STATE_BLOCKED 6

typedef struct {
    uint8_t used;
    uint64_t addr;
    uint32_t size;
} user_alloc_t;

typedef struct __attribute__((aligned(16))) {
    uint8_t state;
    uint8_t is_thread;
    uint8_t thread_detached;
    process_capability_mask_t capability_mask;
    uint64_t entry;
    uint64_t saved_rsp;
    uint64_t saved_user_rsp;
    uint8_t  fpu_state[PROCESS_FPU_STATE_SIZE] __attribute__((aligned(16)));

    uint64_t fs_base;

    uint64_t cr3;
    uint8_t *kernel_stack_base;
    uint64_t kernel_stack_top;
    uint64_t user_code_base;
    uint64_t user_code_limit;
    uint64_t user_heap_base;
    uint64_t user_heap_cursor;
    uint64_t user_heap_limit;
    uint64_t user_heap_alloc_limit;
    uint64_t user_heap_guard_page;
    uint64_t user_stack_base;
    uint64_t user_stack_top;
    uint64_t user_stack_guard_page;
    uint32_t timeslice;
    uint64_t total_ticks;
    char     name[64];
    char     launch_argument[512];
    int32_t parent_pid;
    int32_t memory_owner_pid;
    int32_t exit_status;
    uint64_t thread_stack_region_base;
    uint64_t thread_stack_region_size;
    user_alloc_t user_allocs[PROCESS_USER_ALLOC_MAX];
    uint64_t signal_handlers[PROCESS_SIGNAL_MAX];
    uint64_t signal_mask;
    uint32_t pending_signals;
    uint64_t clear_child_tid;
    uint64_t robust_list_head;
    uint64_t robust_list_length;
} process_t;

void process_scheduler_init(uint32_t timeslice_ticks);
int32_t process_scheduler_current_pid(void);
void process_scheduler_set_current_pid(int32_t pid);
int32_t process_scheduler_pick_next(process_t *processes,
                                    int32_t capacity,
                                    int32_t current_pid);
void process_scheduler_on_tick(process_t *processes, int32_t capacity);
void process_scheduler_request_reschedule(void);
int process_scheduler_consume_reschedule(void);
void process_scheduler_prepare_run(process_t *proc);
