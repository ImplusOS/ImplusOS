#pragma once
#include <stdint.h>

typedef struct kvm_run kvm_run_t;

void vm_devices_init(void);

void vm_handle_io(kvm_run_t *run);
void vm_handle_mmio(kvm_run_t *run);

void vm_devices_set_window(uint32_t window_id);
void vm_devices_redraw(uint64_t exit_count);

void vm_devices_inject_scancode(uint8_t scancode);
int  vm_devices_has_ps2_data(void);

uint8_t vm_devices_get_post_code(void);