#include "KVM_Client.h"
#include "Platform/VTx/VTx.h"
#include "Platform/VTx/EPT.h"
#include "Memory/Memory_Main.h"
#include "ProcessManager/ProcessManager.h"
#include "Sync/Spinlock.h"
#include "Debbuger/Serial/Serial.h"
#include <stddef.h>
#include <string.h>

#define KVM_MAX_CLIENTS 4

typedef struct {
    uint8_t  used;
    int32_t  vm_id;
} kvm_client_t;

static kvm_client_t g_clients[KVM_MAX_CLIENTS];
static spinlock_t   g_kvm_lock;
static int          g_kvm_init_done = 0;

void kvm_client_init(void)
{
    spinlock_init(&g_kvm_lock);
    memset(g_clients, 0, sizeof(g_clients));
    for (int i = 0; i < KVM_MAX_CLIENTS; i++) {
        g_clients[i].vm_id = -1;
    }
    g_kvm_init_done = 1;
}

int64_t kvm_client_open(void)
{
    if (!g_kvm_init_done) kvm_client_init();
    if (!vtx_is_available()) return -95;

    spinlock_lock(&g_kvm_lock);
    for (int i = 0; i < KVM_MAX_CLIENTS; i++) {
        if (!g_clients[i].used) {
            g_clients[i].used = 1;
            g_clients[i].vm_id = -1;
            spinlock_unlock(&g_kvm_lock);
            return (int64_t)(KVM_DEV_FD_BASE + i);
        }
    }
    spinlock_unlock(&g_kvm_lock);
    return -24;
}

static kvm_client_t *fd_to_client(int32_t fd)
{
    int idx = fd - KVM_DEV_FD_BASE;
    if (idx < 0 || idx >= KVM_MAX_CLIENTS) return NULL;
    if (!g_clients[idx].used) return NULL;
    return &g_clients[idx];
}

int64_t kvm_client_ioctl(int32_t fd, uint64_t request, uint64_t arg)
{
    kvm_client_t *c = fd_to_client(fd);
    if (!c) return -22;

    switch (request) {
        case KVM_IOCTL_GET_VERSION: {
            return 1;
        }

        case KVM_IOCTL_CREATE_VM: {
            if (c->vm_id >= 0) return -16;
            int32_t vm_id = vtx_vm_create();
            if (vm_id < 0) return -12;
            c->vm_id = vm_id;
            return (int64_t)vm_id;
        }

        case KVM_IOCTL_DESTROY_VM: {
            if (c->vm_id < 0) return -22;
            vtx_vm_destroy(c->vm_id);
            c->vm_id = -1;
            return 0;
        }

        case KVM_IOCTL_SET_REGS: {
            if (c->vm_id < 0) return -22;
            vmx_regs_t *regs = (vmx_regs_t *)(uintptr_t)arg;
            if (!regs) return -14;
            return vtx_vm_set_regs(c->vm_id, regs);
        }

        case KVM_IOCTL_GET_REGS: {
            if (c->vm_id < 0) return -22;
            vmx_regs_t *regs = (vmx_regs_t *)(uintptr_t)arg;
            if (!regs) return -14;
            return vtx_vm_get_regs(c->vm_id, regs);
        }

        case KVM_IOCTL_MAP_MEMORY: {
            if (c->vm_id < 0) return -22;
            vmx_memory_region_t *region = (vmx_memory_region_t *)(uintptr_t)arg;
            if (!region) return -14;
            return vtx_vm_map_memory(c->vm_id, region);
        }

        case KVM_IOCTL_RUN: {
            if (c->vm_id < 0) return -22;
            return vtx_vm_run(c->vm_id);
        }

        case KVM_IOCTL_SET_FRAMEBUFFER: {
            if (c->vm_id < 0) return -22;
            vmx_framebuffer_t *fb = (vmx_framebuffer_t *)(uintptr_t)arg;
            if (!fb) return -14;
            return vtx_vm_set_framebuffer(c->vm_id, fb);
        }

        case KVM_IOCTL_GET_EXIT_INFO: {
            if (c->vm_id < 0) return -22;
            vmx_exit_info_t *info = (vmx_exit_info_t *)(uintptr_t)arg;
            if (!info) return -14;
            return vtx_vm_get_exit_info(c->vm_id, info);
        }

        case KVM_IOCTL_SET_IO_RESPONSE: {
            if (c->vm_id < 0) return -22;
            uint32_t data = (uint32_t)arg;
            return vtx_vm_set_io_response(c->vm_id, data);
        }

        default:
            return -22;
    }
}

int64_t kvm_client_close(int32_t fd)
{
    kvm_client_t *c = fd_to_client(fd);
    if (!c) return -22;

    spinlock_lock(&g_kvm_lock);
    if (c->vm_id >= 0) {
        vtx_vm_destroy(c->vm_id);
        c->vm_id = -1;
    }
    c->used = 0;
    spinlock_unlock(&g_kvm_lock);
    return 0;
}

void *kvm_client_mmap(int32_t fd, uint64_t offset, uint64_t size)
{
    (void)fd;
    (void)size;
    return (void *)(uintptr_t)offset;
}
