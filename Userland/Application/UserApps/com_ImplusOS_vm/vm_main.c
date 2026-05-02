/*
 * vm_main.c — Userland OVMF boot loader
 *
 * Loads OVMF_CODE_4M.fd into guest memory and boots it using the
 * ImplusOS KVM-style VMX interface. Uses Unrestricted Guest mode
 * to start in real mode at the x86 reset vector (0xFFFFFFF0).
 *
 * Memory Layout:
 *   GPA 0x00000000 – 0x07FFFFFF : 128MB guest RAM
 *   GPA 0xFF800000 – 0xFFBFFFFF :   4MB OVMF_CODE.fd (firmware, read-only)
 *   GPA 0xFFC00000 – 0xFFFFFFFF :   4MB OVMF_VARS (NV storage, zeroed)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/syscalls.h>
#include "../../../Syscalls.h"
#include "../../../API/KVM.h"

/* ── KVM ioctl commands ────────────────────────────────────── */
#define KVM_CREATE_VM               1
#define KVM_CREATE_VCPU             2
#define KVM_SET_USER_MEMORY_REGION  3
#define KVM_RUN                     4
#define KVM_GET_REGS                5
#define KVM_SET_REGS                6
#define KVM_GET_SREGS               7
#define KVM_SET_SREGS               8

/* ── KVM exit reasons ──────────────────────────────────────── */
#define KVM_EXIT_UNKNOWN            0
#define KVM_EXIT_IO                 2
#define KVM_EXIT_DEBUG              4
#define KVM_EXIT_HLT                5
#define KVM_EXIT_MMIO               6
#define KVM_EXIT_SHUTDOWN           8
#define KVM_EXIT_INTERNAL_ERROR     17

/* ── Shared types (must match kernel definitions) ──────────── */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
} vmx_regs_t;

typedef struct {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint8_t  type;
    uint8_t  present;
    uint8_t  dpl;
    uint8_t  db;
    uint8_t  s;
    uint8_t  l;
    uint8_t  g;
    uint8_t  avl;
    uint8_t  unusable;
    uint8_t  _pad;
} vmx_segment_t;

typedef struct {
    uint64_t base;
    uint16_t limit;
    uint16_t _pad[3];
} vmx_dtable_t;

typedef struct {
    vmx_segment_t cs, ds, es, fs, gs, ss;
    vmx_segment_t tr, ldt;
    vmx_dtable_t  gdt, idt;
    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
} vmx_sregs_t;

typedef struct {
    uint32_t slot;
    uint32_t flags;
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
} kvm_userspace_memory_region_t;

typedef struct kvm_run {
    uint8_t  request_interrupt_window;
    uint8_t  immediate_exit;
    uint8_t  _pad_in[6];

    uint32_t exit_reason;
    uint8_t  ready_for_interrupt_injection;
    uint8_t  _pad_out[3];

    union {
        struct {
            uint8_t  direction;
            uint8_t  size;
            uint16_t port;
            uint32_t count;
            uint64_t data_offset;
        } io;
        struct {
            uint64_t phys_addr;
            uint8_t  data[8];
            uint32_t len;
            uint8_t  is_write;
            uint8_t  _pad[3];
        } mmio;
        struct {
            uint32_t suberror;
            uint32_t ndata;
            uint64_t data[16];
        } internal;
        uint8_t _pad_exit[256];
    };
    uint8_t io_data[64];
} kvm_run_t;

/* ── Device emulation (from vm_devices.c) ──────────────────── */
extern void vm_devices_init(void);
extern void vm_handle_io(kvm_run_t *run);
extern void vm_handle_mmio(kvm_run_t *run);

/* ── Guest memory configuration ────────────────────────────── */
#define GUEST_RAM_SIZE      (128ULL * 1024 * 1024)  /* 128MB */
#define OVMF_CODE_SIZE      (4ULL * 1024 * 1024)    /* 4MB */
#define OVMF_VARS_SIZE      (4ULL * 1024 * 1024)    /* 4MB */

/* OVMF 4M maps to the top of the 32-bit address space:
 *   CODE: 0xFF800000 – 0xFFBFFFFF (4MB)
 *   VARS: 0xFFC00000 – 0xFFFFFFFF (4MB)
 * The reset vector is at 0xFFFFFFF0 (within VARS region end area,
 * but OVMF's actual reset vector jump target is in CODE).
 * For a 4M build: the entire 4MB firmware starts at 0xFF800000,
 * and 0xFFFFFFF0 = VARS_BASE + VARS_SIZE - 0x10.
 * Actually for OVMF 4M: total flash = 4MB CODE + 540KB VARS.
 * But the standard layout puts CODE at top-of-4G minus CODE_SIZE.
 * Let's map CODE at 0xFFC00000 (standard for 4MB image mapped at
 * top of 32-bit space) so that 0xFFFFFFF0 falls inside it. */
#define OVMF_CODE_GPA       0xFFC00000ULL           /* CODE mapped here */
#define OVMF_VARS_GPA       0x00000000ULL           /* VARS in low memory (optional area) */

/* OVMF_CODE_4M.fd is exactly 3653632 bytes (0x37C000) = ~3.5MB.
 * It contains the reset vector at offset (size - 0x10).
 * When mapped at GPA 0xFFC00000, the reset vector lands at:
 *   GPA = 0xFFC00000 + 0x37C000 - 0x10 = 0xFFF7BFF0 ... that's wrong.
 * 
 * The correct approach: OVMF firmware is always mapped at the TOP of 4GB.
 * For a 3.5MB firmware: GPA = 0x100000000 - 0x380000 = 0xFFC80000
 * For OVMF_CODE_4M.fd (3653632 = 0x37C000 bytes):
 *   GPA_start = 0x100000000 - 0x400000 = 0xFFC00000
 *   But the code is only 0x37C000, so we need to map it at:
 *   GPA_start = 0x100000000 - 0x37C000 = 0xFFC84000
 * 
 * Actually, OVMF builds pad to flash size. For "4M" variant:
 *   OVMF_CODE_4M.fd = 3653632 bytes (3.5MB, the CODE portion)
 *   OVMF_VARS_4M.fd = 540672 bytes (528KB, the VARS portion)
 *   Total = 3653632 + 540672 = 4194304 = exactly 4MB
 *   So combined they fill 0xFFC00000 – 0xFFFFFFFF.
 *
 * Layout:
 *   VARS: 0xFFC00000 – 0xFFC83FFF (540672 bytes)
 *   CODE: 0xFFC84000 – 0xFFFFFFFF (3653632 bytes)
 *   Reset vector at 0xFFFFFFF0 = CODE_GPA + CODE_SIZE - 0x10
 */
#define OVMF_FW_BASE        0xFFC00000ULL           /* Combined firmware base */
#define OVMF_FW_TOTAL       (4ULL * 1024 * 1024)    /* 4MB total flash */
#define OVMF_VARS_FW_SIZE   540672ULL               /* OVMF_VARS_4M.fd size */
#define OVMF_CODE_FW_SIZE   3653632ULL              /* OVMF_CODE_4M.fd size */
#define OVMF_RESET_REAL_RIP 0xFF10ULL

/* ── OVMF file path ────────────────────────────────────────── */
#define OVMF_CODE_PATH "/Userland/UserApps/com_ImplusOS_vm/Resource/OVMF_CODE_4M.fd"
#define OVMF_VARS_PATH "/Userland/UserApps/com_ImplusOS_vm/Resource/OVMF_VARS_4M.fd"

#define CR0_ET (1ULL << 4)
#define CR0_NE (1ULL << 5)

static void setup_real_mode_segment(vmx_segment_t *seg,
                                    uint16_t selector,
                                    uint64_t base,
                                    uint32_t limit,
                                    uint8_t type,
                                    uint8_t s)
{
    memset(seg, 0, sizeof(*seg));
    seg->selector = selector;
    seg->base = base;
    seg->limit = limit;
    seg->type = type;
    seg->present = 1;
    seg->s = s;
}

static void setup_unusable_segment(vmx_segment_t *seg)
{
    memset(seg, 0, sizeof(*seg));
    seg->unusable = 1;
}

/* ── Helper: load file into buffer ─────────────────────────── */
static int64_t load_file(const char *path, uint8_t *buf, uint64_t max_size)
{
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        printf("[VM] Failed to open %s (err=%d)\n", path, fd);
        return -1;
    }

    int64_t total = 0;
    while ((uint64_t)total < max_size) {
        uint64_t chunk = max_size - (uint64_t)total;
        if (chunk > 32768) chunk = 32768;
        int64_t n = file_read(fd, buf + total, chunk);
        if (n <= 0) break;
        total += n;
    }

    file_close(fd);
    return total;
}

/* ── Main ──────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("[VM] ImplusOS Userland VM — OVMF Boot\n");

    /* Initialize device emulation */
    vm_devices_init();

    /* 1. Open KVM */
    int32_t kvm_fd = kvm_open();
    if (kvm_fd < 0) {
        printf("[VM] Failed to open KVM (err=%d)\n", kvm_fd);
        process_exit(1);
    }

    if (kvm_ioctl(kvm_fd, KVM_CREATE_VM, 0) < 0) {
        printf("[VM] Failed to create VM\n");
        process_exit(1);
    }

    /* 2. Allocate guest RAM (128MB) */
    printf("[VM] Allocating %llu MB guest RAM...\n",
           (unsigned long long)(GUEST_RAM_SIZE / (1024 * 1024)));
    void *guest_ram = os_mmap(GUEST_RAM_SIZE, 0);
    if (!guest_ram) {
        printf("[VM] Failed to allocate guest RAM\n");
        process_exit(1);
    }
    memset(guest_ram, 0, GUEST_RAM_SIZE);

    /* 3. Allocate firmware region (4MB for combined CODE+VARS flash) */
    void *fw_mem = os_mmap(OVMF_FW_TOTAL, 0);
    if (!fw_mem) {
        printf("[VM] Failed to allocate firmware memory\n");
        process_exit(1);
    }
    memset(fw_mem, 0xFF, OVMF_FW_TOTAL); /* Flash default = 0xFF */

    /* 4. Load OVMF_VARS_4M.fd into the beginning of firmware region */
    printf("[VM] Loading OVMF_VARS_4M.fd...\n");
    int64_t vars_loaded = load_file(OVMF_VARS_PATH,
                                     (uint8_t *)fw_mem,
                                     OVMF_VARS_FW_SIZE);
    if (vars_loaded > 0) {
        printf("[VM] OVMF_VARS loaded: %lld bytes\n", (long long)vars_loaded);
    } else {
        printf("[VM] OVMF_VARS not found, using zeroed NV store\n");
        /* Fill VARS area with 0xFF (erased flash state) */
        memset(fw_mem, 0xFF, OVMF_VARS_FW_SIZE);
    }

    /* 5. Load OVMF_CODE_4M.fd into firmware region after VARS */
    printf("[VM] Loading OVMF_CODE_4M.fd...\n");
    int64_t code_loaded = load_file(OVMF_CODE_PATH,
                                     (uint8_t *)fw_mem + OVMF_VARS_FW_SIZE,
                                     OVMF_CODE_FW_SIZE);
    if (code_loaded <= 0) {
        printf("[VM] ERROR: Failed to load OVMF_CODE_4M.fd!\n");
        process_exit(1);
    }
    printf("[VM] OVMF_CODE loaded: %lld bytes\n", (long long)code_loaded);

    /* Verify reset vector exists */
    uint8_t *reset_vec = (uint8_t *)fw_mem + OVMF_FW_TOTAL - 0x10;
    printf("[VM] Reset vector bytes: %02x %02x %02x %02x\n",
           reset_vec[0], reset_vec[1], reset_vec[2], reset_vec[3]);

    /* 6. Create vCPU */
    if (kvm_ioctl(kvm_fd, KVM_CREATE_VCPU, 0) < 0) {
        printf("[VM] Failed to create vCPU\n");
        process_exit(1);
    }

    /* 7. Set up memory regions */
    /* Slot 0: Guest RAM at GPA 0 */
    kvm_userspace_memory_region_t ram_region = {
        .slot = 0,
        .flags = 0,
        .guest_phys_addr = 0x00000000ULL,
        .memory_size = GUEST_RAM_SIZE,
        .userspace_addr = (uint64_t)(uintptr_t)guest_ram
    };
    if (kvm_ioctl(kvm_fd, KVM_SET_USER_MEMORY_REGION,
                  (uint64_t)(uintptr_t)&ram_region) < 0) {
        printf("[VM] Failed to set RAM region\n");
        process_exit(1);
    }

    /* Slot 1: Firmware at GPA 0xFFC00000 (top of 32-bit space) */
    kvm_userspace_memory_region_t fw_region = {
        .slot = 1,
        .flags = 0,
        .guest_phys_addr = OVMF_FW_BASE,
        .memory_size = OVMF_FW_TOTAL,
        .userspace_addr = (uint64_t)(uintptr_t)fw_mem
    };
    if (kvm_ioctl(kvm_fd, KVM_SET_USER_MEMORY_REGION,
                  (uint64_t)(uintptr_t)&fw_region) < 0) {
        printf("[VM] Failed to set firmware region\n");
        process_exit(1);
    }

    /* 8. Get kvm_run shared page */
    kvm_run_t *run = (kvm_run_t *)kvm_mmap(kvm_fd, 0, 4096);
    if (!run) {
        printf("[VM] Failed to mmap kvm_run\n");
        process_exit(1);
    }

    /* 9. Set full guest reset state for x86 real-mode firmware entry */
    struct { uint32_t vcpu; vmx_sregs_t sregs; } sregs_cmd;
    memset(&sregs_cmd, 0, sizeof(sregs_cmd));
    sregs_cmd.vcpu = 0;

    sregs_cmd.sregs.cr0 = CR0_ET | CR0_NE;
    sregs_cmd.sregs.cr3 = 0;
    sregs_cmd.sregs.cr4 = 0;
    sregs_cmd.sregs.efer = 0;

    setup_real_mode_segment(&sregs_cmd.sregs.cs, 0xF000, 0xFFFF0000ULL, 0xFFFF, 0xB, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.ds, 0x0000, 0x00000000ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.es, 0x0000, 0x00000000ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.fs, 0x0000, 0x00000000ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.gs, 0x0000, 0x00000000ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.ss, 0x0000, 0x00000000ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.tr, 0x0000, 0x00000000ULL, 0xFFFF, 0xB, 0);
    setup_unusable_segment(&sregs_cmd.sregs.ldt);

    sregs_cmd.sregs.gdt.base = 0;
    sregs_cmd.sregs.gdt.limit = 0xFFFF;
    sregs_cmd.sregs.idt.base = 0;
    sregs_cmd.sregs.idt.limit = 0xFFFF;

    if (kvm_ioctl(kvm_fd, KVM_SET_SREGS,
                  (uint64_t)(uintptr_t)&sregs_cmd) < 0) {
        printf("[VM] Failed to set sregs\n");
        process_exit(1);
    }

    /* 10. Set initial general-purpose registers */
    struct { uint32_t vcpu; vmx_regs_t regs; } regs_cmd;
    regs_cmd.vcpu = 0;
    memset(&regs_cmd.regs, 0, sizeof(regs_cmd.regs));
    /* Nested KVM on the current host faults on the reset vector's initial
     * MOV-from-CR0 sequence. Enter at the real-mode branch target instead. */
    regs_cmd.regs.rax = CR0_ET | CR0_NE;
    regs_cmd.regs.rip = OVMF_RESET_REAL_RIP;
    regs_cmd.regs.rflags = 0x02;  /* Reserved bit 1 always set */
    regs_cmd.regs.rdx = 0x0600;   /* CPUID family/model hint */

    if (kvm_ioctl(kvm_fd, KVM_SET_REGS,
                  (uint64_t)(uintptr_t)&regs_cmd) < 0) {
        printf("[VM] Failed to set regs\n");
        process_exit(1);
    }

    /* 11. Run the VM */
    printf("[VM] Starting OVMF...\n");
    printf("[VM] Reset: CS:IP = F000:%04llx -> linear 0x%08llx\n",
           (unsigned long long)OVMF_RESET_REAL_RIP,
           (unsigned long long)(0xFFFF0000ULL + OVMF_RESET_REAL_RIP));

    uint64_t exit_count = 0;
    uint64_t io_count = 0;
    uint64_t mmio_count = 0;
    int running = 1;

    while (running) {
        int ret = (int)kvm_ioctl(kvm_fd, KVM_RUN, 0);
        exit_count++;

        if (ret < 0) {
            printf("[VM] KVM_RUN failed (ret=%d)\n", ret);
            /* Check kvm_run for details */
            if (run->exit_reason == KVM_EXIT_INTERNAL_ERROR) {
                printf("[VM] Internal error: suberror=%u\n",
                       run->internal.suberror);
            }
            break;
        }

        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            io_count++;
            vm_handle_io(run);
            break;

        case KVM_EXIT_MMIO:
            mmio_count++;
            vm_handle_mmio(run);
            break;

        case KVM_EXIT_HLT:
            /* Guest executed HLT — yield and retry */
            process_yield();
            break;

        case KVM_EXIT_SHUTDOWN:
            printf("[VM] Guest shutdown (triple fault)\n");
            /* Dump state for debugging */
            regs_cmd.vcpu = 0;
            kvm_ioctl(kvm_fd, KVM_GET_REGS,
                      (uint64_t)(uintptr_t)&regs_cmd);
            printf("[VM]   RIP=0x%llx RSP=0x%llx\n",
                   (unsigned long long)regs_cmd.regs.rip,
                   (unsigned long long)regs_cmd.regs.rsp);
            printf("[VM]   RAX=0x%llx RBX=0x%llx\n",
                   (unsigned long long)regs_cmd.regs.rax,
                   (unsigned long long)regs_cmd.regs.rbx);
            running = 0;
            break;

        case KVM_EXIT_INTERNAL_ERROR:
            printf("[VM] Internal error: suberror=%u\n",
                   run->internal.suberror);
            running = 0;
            break;

        case KVM_EXIT_DEBUG:
            printf("[VM] Debug exit\n");
            break;

        default:
            printf("[VM] Unknown exit reason: %u\n", run->exit_reason);
            running = 0;
            break;
        }

        /* Periodic status (every 100000 exits) */
        if (exit_count % 100000 == 0) {
            printf("[VM] Status: exits=%llu io=%llu mmio=%llu\n",
                   (unsigned long long)exit_count,
                   (unsigned long long)io_count,
                   (unsigned long long)mmio_count);
        }
    }

    printf("[VM] VM stopped after %llu exits (io=%llu mmio=%llu)\n",
           (unsigned long long)exit_count,
           (unsigned long long)io_count,
           (unsigned long long)mmio_count);

    kvm_close(kvm_fd);
    process_exit(0);
    return 0;
}
