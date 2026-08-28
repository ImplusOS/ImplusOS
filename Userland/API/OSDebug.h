#pragma once

#include <stdint.h>

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    uint8_t state;
    uint8_t reserved[7];
    char name[64];
    uint64_t page_fault_count;
    uint64_t last_pf_addr;
    uint64_t last_pf_rip;
    uint32_t last_pf_error;
    uint8_t has_crashed;
    char crash_reason[64];
    int32_t exit_status;
} process_debug_info_t;

typedef struct {
    uint64_t total_page_faults;
    uint32_t process_count;
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t used_memory;
} os_debug_info_t;

int32_t get_process_debug_info(int32_t pid, process_debug_info_t *info_out);
int32_t get_os_debug_info(os_debug_info_t *info_out);
