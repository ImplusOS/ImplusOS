#pragma once
#include <stdint.h>

/* ── KVM device file descriptor base ──────────────────────────── */
#define KVM_DEV_FD_BASE        0x7000

/* ── IOCTL request numbers ────────────────────────────────────── */
#define KVM_IOCTL_GET_VERSION      0x00
#define KVM_IOCTL_CREATE_VM        0x01
#define KVM_IOCTL_DESTROY_VM       0x02
#define KVM_IOCTL_SET_REGS         0x03
#define KVM_IOCTL_GET_REGS         0x04
#define KVM_IOCTL_MAP_MEMORY       0x05
#define KVM_IOCTL_RUN              0x06
#define KVM_IOCTL_SET_FRAMEBUFFER  0x07
#define KVM_IOCTL_GET_EXIT_INFO    0x08
#define KVM_IOCTL_SET_IO_RESPONSE  0x09

/* ── API (called from syscall dispatch) ───────────────────────── */
void    kvm_client_init(void);
int64_t kvm_client_open(void);
int64_t kvm_client_ioctl(int32_t fd, uint64_t request, uint64_t arg);
int64_t kvm_client_close(int32_t fd);
void   *kvm_client_mmap(int32_t fd, uint64_t offset, uint64_t size);
