#pragma once
#include <stdint.h>

/* Forward declaration for kvm_run_t (defined in vm_main.c) */
typedef struct kvm_run kvm_run_t;

/* ── Device emulation API ─────────────────────────────────── */
void vm_devices_init(void);
void vm_handle_io(kvm_run_t *run);
void vm_handle_mmio(kvm_run_t *run);
