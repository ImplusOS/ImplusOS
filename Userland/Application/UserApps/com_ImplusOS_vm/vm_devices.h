#pragma once
#include <stdint.h>

 
typedef struct kvm_run kvm_run_t;

 
void vm_devices_init(void);
void vm_handle_io(kvm_run_t *run);
void vm_handle_mmio(kvm_run_t *run);
