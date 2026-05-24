#pragma once

#include <stdint.h>

typedef void (*signal_handler_t)(int32_t signum);

signal_handler_t signal(int32_t signum, signal_handler_t handler);
void process_yield(void);
int32_t process_get_current_pid(void);
int32_t process_spawn(const char *path);
int32_t process_waitpid(int32_t pid, int32_t *status_out, int32_t options);
int32_t process_getppid(void);
void process_exit(int32_t status);
void system_shutdown(void);
void system_reboot(void);
void sleep_ms(uint64_t milliseconds);
uint64_t get_uptime_ms(void);

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    uint8_t state;
    uint8_t reserved[3];
} process_info_t;

int32_t get_process_count(void);
int32_t get_process_info(int32_t pid, process_info_t *info_out);
