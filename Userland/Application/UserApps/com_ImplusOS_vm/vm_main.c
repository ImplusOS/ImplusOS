#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/syscalls.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include "../../../Syscalls.h"
#include "../../../API/KVM.h"
#include "../../../API/Serial.h"
#include "vm_devices.h"

static void vm_log(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    serial_write_string(buf);
}

#define KVM_CREATE_VM               1
#define KVM_CREATE_VCPU             2
#define KVM_SET_USER_MEMORY_REGION  3
#define KVM_RUN                     4
#define KVM_GET_REGS                5
#define KVM_SET_REGS                6
#define KVM_GET_SREGS               7
#define KVM_SET_SREGS               8

#define KVM_EXIT_UNKNOWN            0
#define KVM_EXIT_IO                 2
#define KVM_EXIT_DEBUG              4
#define KVM_EXIT_HLT                5
#define KVM_EXIT_MMIO               6
#define KVM_EXIT_SHUTDOWN           8
#define KVM_EXIT_INTERNAL_ERROR     17

/* ── Register structures (must match kernel VMX.h) ─────────────── */
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
    uint8_t  type, present, dpl, db, s, l, g, avl, unusable, _pad;
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
    uint32_t slot, flags;
    uint64_t guest_phys_addr, memory_size, userspace_addr;
} kvm_userspace_memory_region_t;

typedef struct kvm_run {
    uint8_t  request_interrupt_window, immediate_exit;
    uint8_t  _pad_in[6];
    uint32_t exit_reason;
    uint8_t  ready_for_interrupt_injection;
    uint8_t  _pad_out[3];
    union {
        struct { uint8_t direction, size; uint16_t port; uint32_t count; uint64_t data_offset; } io;
        struct { uint64_t phys_addr; uint8_t data[8]; uint32_t len; uint8_t is_write; uint8_t _pad[3]; } mmio;
        struct { uint32_t suberror, ndata; uint64_t data[16]; } internal;
        uint8_t _pad_exit[256];
    };
    uint8_t io_data[64];
} kvm_run_t;

#define GUEST_RAM_SIZE      (128ULL * 1024 * 1024)
#define OVMF_FW_BASE        0xFFC00000ULL
#define OVMF_FW_TOTAL       (4ULL * 1024 * 1024)
#define OVMF_VARS_FW_SIZE   540672ULL
#define OVMF_CODE_FW_SIZE   3653632ULL

#define OVMF_CODE_PATH "/Userland/UserApps/com_ImplusOS_vm/Resource/OVMF_CODE_4M.fd"
#define OVMF_VARS_PATH "/Userland/UserApps/com_ImplusOS_vm/Resource/OVMF_VARS_4M.fd"

#define WIN_WIDTH   800
#define WIN_HEIGHT  580

static const uint8_t keycode_to_set1[] = {
    /* 0x00 */ 0x00,
    /* 0x01 ESC */ 0x01,
    /* 0x02 1 */ 0x02, /* 0x03 2 */ 0x03, /* 0x04 3 */ 0x04,
    /* 0x05 4 */ 0x05, /* 0x06 5 */ 0x06, /* 0x07 6 */ 0x07,
    /* 0x08 7 */ 0x08, /* 0x09 8 */ 0x09, /* 0x0A 9 */ 0x0A,
    /* 0x0B 0 */ 0x0B, /* 0x0C - */ 0x0C, /* 0x0D = */ 0x0D,
    /* 0x0E BS */ 0x0E, /* 0x0F Tab */ 0x0F,
    /* 0x10 Q */ 0x10, /* 0x11 W */ 0x11, /* 0x12 E */ 0x12,
    /* 0x13 R */ 0x13, /* 0x14 T */ 0x14, /* 0x15 Y */ 0x15,
    /* 0x16 U */ 0x16, /* 0x17 I */ 0x17, /* 0x18 O */ 0x18,
    /* 0x19 P */ 0x19, /* 0x1A [ */ 0x1A, /* 0x1B ] */ 0x1B,
    /* 0x1C Enter */ 0x1C,
    /* 0x1D LCtrl */ 0x1D,
    /* 0x1E A */ 0x1E, /* 0x1F S */ 0x1F, /* 0x20 D */ 0x20,
    /* 0x21 F */ 0x21, /* 0x22 G */ 0x22, /* 0x23 H */ 0x23,
    /* 0x24 J */ 0x24, /* 0x25 K */ 0x25, /* 0x26 L */ 0x26,
    /* 0x27 ; */ 0x27, /* 0x28 ' */ 0x28, /* 0x29 ` */ 0x29,
    /* 0x2A LShift */ 0x2A, /* 0x2B \ */ 0x2B,
    /* 0x2C Z */ 0x2C, /* 0x2D X */ 0x2D, /* 0x2E C */ 0x2E,
    /* 0x2F V */ 0x2F, /* 0x30 B */ 0x30, /* 0x31 N */ 0x31,
    /* 0x32 M */ 0x32, /* 0x33 , */ 0x33, /* 0x34 . */ 0x34,
    /* 0x35 / */ 0x35, /* 0x36 RShift */ 0x36,
    /* 0x37 KP* */ 0x37, /* 0x38 LAlt */ 0x38,
    /* 0x39 Space */ 0x39, /* 0x3A Caps */ 0x3A,
    /* 0x3B F1 */ 0x3B, /* 0x3C F2 */ 0x3C, /* 0x3D F3 */ 0x3D,
    /* 0x3E F4 */ 0x3E, /* 0x3F F5 */ 0x3F, /* 0x40 F6 */ 0x40,
    /* 0x41 F7 */ 0x41, /* 0x42 F8 */ 0x42, /* 0x43 F9 */ 0x43,
    /* 0x44 F10 */ 0x44,
};
#define SET1_TABLE_SIZE (sizeof(keycode_to_set1) / sizeof(keycode_to_set1[0]))

/* Extended keys that need E0 prefix */
static void inject_host_key(uint16_t keycode, uint8_t pressed)
{
    /* Extended keys (arrows, etc.) */
    uint8_t sc = 0;
    int extended = 0;

    switch (keycode) {
    case 0x48: sc = 0x48; extended = 1; break; /* Up */
    case 0x50: sc = 0x50; extended = 1; break; /* Down */
    case 0x4B: sc = 0x4B; extended = 1; break; /* Left */
    case 0x4D: sc = 0x4D; extended = 1; break; /* Right */
    case 0x47: sc = 0x47; extended = 1; break; /* Home */
    case 0x4F: sc = 0x4F; extended = 1; break; /* End */
    case 0x49: sc = 0x49; extended = 1; break; /* PgUp */
    case 0x51: sc = 0x51; extended = 1; break; /* PgDn */
    case 0x52: sc = 0x52; extended = 1; break; /* Insert */
    case 0x53: sc = 0x53; extended = 1; break; /* Delete */
    default:
        if (keycode < SET1_TABLE_SIZE) {
            sc = keycode_to_set1[keycode];
        }
        break;
    }

    if (sc == 0) return;

    if (extended) {
        vm_devices_inject_scancode(0xE0);
    }
    if (pressed) {
        vm_devices_inject_scancode(sc);
    } else {
        vm_devices_inject_scancode((uint8_t)(sc | 0x80));
    }
}

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
    seg->present = 0;
}

static int64_t load_file(const char *path, uint8_t *buf, uint64_t max_size)
{
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        vm_log("[VM] Failed to open %s (err=%d)\n", path, fd);
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

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    vm_log("[VM] ImplusOS VM — OVMF Boot\n");

    window_id_t win = window_create(WIN_WIDTH, WIN_HEIGHT, "ImplusOS VM");
    if (win == 0) {
        vm_log("[VM] Failed to create window\n");
        process_exit(1);
    }
    window_show(win);
    window_set_focus(win);
    window_subscribe_keyboard(win);

    graphics_init(win);
    vm_devices_init();
    vm_devices_set_window(win);

    window_set_bg_color(win, 0xFF1E1E2E);
    window_draw_text(win, 8, 4,
                     "ImplusOS VM — Booting OVMF...", 0xFF89B4FA, 13.0f);
    draw_present();

    int32_t kvm_fd = kvm_open();
    if (kvm_fd < 0) {
        vm_log("[VM] Failed to open KVM (err=%d)\n", kvm_fd);
        process_exit(1);
    }

    if (kvm_ioctl(kvm_fd, KVM_CREATE_VM, 0) < 0) {
        vm_log("[VM] Failed to create VM\n");
        process_exit(1);
    }

    vm_log("[VM] Allocating %llu MB guest RAM...\n",
           (unsigned long long)(GUEST_RAM_SIZE / (1024 * 1024)));
    void *guest_ram = os_mmap(GUEST_RAM_SIZE, 0);
    if (!guest_ram) {
        vm_log("[VM] Failed to allocate guest RAM\n");
        process_exit(1);
    }
    memset(guest_ram, 0, GUEST_RAM_SIZE);

    void *fw_mem = os_mmap(OVMF_FW_TOTAL, 0);
    if (!fw_mem) {
        vm_log("[VM] Failed to allocate firmware memory\n");
        process_exit(1);
    }
    memset(fw_mem, 0xFF, OVMF_FW_TOTAL);

    vm_log("[VM] Loading OVMF_VARS_4M.fd...\n");
    int64_t vars_loaded = load_file(OVMF_VARS_PATH,
                                     (uint8_t *)fw_mem,
                                     OVMF_VARS_FW_SIZE);
    if (vars_loaded > 0) {
        vm_log("[VM] OVMF_VARS loaded: %lld bytes\n", (long long)vars_loaded);
    } else {
        vm_log("[VM] OVMF_VARS not found, using 0xFF NV store\n");
        memset(fw_mem, 0xFF, OVMF_VARS_FW_SIZE);
    }

    vm_log("[VM] Loading OVMF_CODE_4M.fd...\n");
    int64_t code_loaded = load_file(OVMF_CODE_PATH,
                                     (uint8_t *)fw_mem + OVMF_VARS_FW_SIZE,
                                     OVMF_CODE_FW_SIZE);
    if (code_loaded <= 0) {
        vm_log("[VM] ERROR: Failed to load OVMF_CODE_4M.fd!\n");
        process_exit(1);
    }
    vm_log("[VM] OVMF_CODE loaded: %lld bytes\n", (long long)code_loaded);

    uint8_t *reset_vec = (uint8_t *)fw_mem + OVMF_FW_TOTAL - 0x10;
    vm_log("[VM] Reset vector: %02x %02x %02x %02x\n",
           reset_vec[0], reset_vec[1], reset_vec[2], reset_vec[3]);

    if (kvm_ioctl(kvm_fd, KVM_CREATE_VCPU, 0) < 0) {
        vm_log("[VM] Failed to create vCPU\n");
        process_exit(1);
    }

    kvm_userspace_memory_region_t ram_region = {
        .slot = 0, .flags = 0,
        .guest_phys_addr = 0x00000000ULL,
        .memory_size = GUEST_RAM_SIZE,
        .userspace_addr = (uint64_t)(uintptr_t)guest_ram
    };
    if (kvm_ioctl(kvm_fd, KVM_SET_USER_MEMORY_REGION,
                  (uint64_t)(uintptr_t)&ram_region) < 0) {
        vm_log("[VM] Failed to set RAM region\n");
        process_exit(1);
    }

    kvm_userspace_memory_region_t fw_region = {
        .slot = 1, .flags = 0,
        .guest_phys_addr = OVMF_FW_BASE,
        .memory_size = OVMF_FW_TOTAL,
        .userspace_addr = (uint64_t)(uintptr_t)fw_mem
    };
    if (kvm_ioctl(kvm_fd, KVM_SET_USER_MEMORY_REGION,
                  (uint64_t)(uintptr_t)&fw_region) < 0) {
        vm_log("[VM] Failed to set firmware region\n");
        process_exit(1);
    }

    kvm_run_t *run = (kvm_run_t *)kvm_mmap(kvm_fd, 0, 4096);
    if (!run) {
        vm_log("[VM] Failed to mmap kvm_run\n");
        process_exit(1);
    }

    struct { uint32_t vcpu; vmx_sregs_t sregs; } sregs_cmd;
    memset(&sregs_cmd, 0, sizeof(sregs_cmd));
    sregs_cmd.vcpu = 0;
    sregs_cmd.sregs.cr0 = 0x00000030ULL;
    sregs_cmd.sregs.cr3 = 0;
    sregs_cmd.sregs.cr4 = 0;
    sregs_cmd.sregs.efer = 0;

    setup_real_mode_segment(&sregs_cmd.sregs.cs, 0xF000, 0xFFFF0000ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.ds, 0x0000, 0x0ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.es, 0x0000, 0x0ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.fs, 0x0000, 0x0ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.gs, 0x0000, 0x0ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.ss, 0x0000, 0x0ULL, 0xFFFF, 0x3, 1);
    setup_real_mode_segment(&sregs_cmd.sregs.tr, 0x0008, 0x0ULL, 0xFFFF, 0x0B, 0);
    setup_unusable_segment(&sregs_cmd.sregs.ldt);

    sregs_cmd.sregs.gdt.base = 0;
    sregs_cmd.sregs.gdt.limit = 0;
    sregs_cmd.sregs.idt.base = 0;
    sregs_cmd.sregs.idt.limit = 0;

    if (kvm_ioctl(kvm_fd, KVM_SET_SREGS,
                  (uint64_t)(uintptr_t)&sregs_cmd) < 0) {
        vm_log("[VM] Failed to set sregs\n");
        process_exit(1);
    }

    struct { uint32_t vcpu; vmx_regs_t regs; } regs_cmd;
    regs_cmd.vcpu = 0;
    memset(&regs_cmd.regs, 0, sizeof(regs_cmd.regs));
    regs_cmd.regs.rip = 0xFFF0;
    regs_cmd.regs.rflags = 0x02;
    regs_cmd.regs.rdx = 0x0600;

    if (kvm_ioctl(kvm_fd, KVM_SET_REGS,
                  (uint64_t)(uintptr_t)&regs_cmd) < 0) {
        vm_log("[VM] Failed to set regs\n");
        process_exit(1);
    }

    vm_log("[VM] Starting OVMF at CS:IP = F000:FFF0\n");

    uint64_t exit_count = 0;
    uint64_t io_count   = 0;
    uint64_t mmio_count = 0;
    int running = 1;
    uint32_t redraw_counter = 0;

    while (running) {
        input_keyboard_event_t kbd_ev;
        while (window_input_keyboard_poll(&kbd_ev) > 0) {
            inject_host_key(kbd_ev.keycode, kbd_ev.pressed);
        }

        int ret = (int)kvm_ioctl(kvm_fd, KVM_RUN, 0);
        exit_count++;

        if (ret < 0) {
            vm_log("[VM] KVM_RUN failed (ret=%d)\n", ret);
            if (run->exit_reason == KVM_EXIT_INTERNAL_ERROR) {
                vm_log("[VM] Internal error: suberror=%u\n",
                       run->internal.suberror);
            }
            break;
        }

        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            io_count++;
            regs_cmd.vcpu = 0;
            kvm_ioctl(kvm_fd, KVM_GET_REGS, (uint64_t)(uintptr_t)&regs_cmd);
            if (io_count <= 1000) {
                vm_log("[VM] IO #%llu: port=0x%03X size=%u dir=%s RIP=0x%llX\n",
                       (unsigned long long)io_count,
                       run->io.port, run->io.size,
                       run->io.direction ? "IN" : "OUT",
                       (unsigned long long)regs_cmd.regs.rip);
            }
            vm_handle_io(run);
            break;

        case KVM_EXIT_MMIO:
            mmio_count++;
            regs_cmd.vcpu = 0;
            kvm_ioctl(kvm_fd, KVM_GET_REGS, (uint64_t)(uintptr_t)&regs_cmd);
            if (mmio_count <= 1000) {
                vm_log("[VM] MMIO #%llu: addr=0x%llX len=%u %s RIP=0x%llX\n",
                       (unsigned long long)mmio_count,
                       (unsigned long long)run->mmio.phys_addr,
                       run->mmio.len,
                       run->mmio.is_write ? "WRITE" : "READ",
                       (unsigned long long)regs_cmd.regs.rip);
            }
            vm_handle_mmio(run);
            break;

        case KVM_EXIT_HLT:
            sleep_ms(5);
            break;

        case KVM_EXIT_SHUTDOWN:
            vm_log("[VM] Guest shutdown (triple fault)\n");
            regs_cmd.vcpu = 0;
            kvm_ioctl(kvm_fd, KVM_GET_REGS,
                      (uint64_t)(uintptr_t)&regs_cmd);
            vm_log("[VM]   RIP=0x%llx RSP=0x%llx\n",
                   (unsigned long long)regs_cmd.regs.rip,
                   (unsigned long long)regs_cmd.regs.rsp);
            running = 0;
            break;

        case KVM_EXIT_INTERNAL_ERROR:
            vm_log("[VM] Internal error: suberror=%u\n",
                   run->internal.suberror);
            running = 0;
            break;

        case KVM_EXIT_DEBUG:
            break;

        default:
            vm_log("[VM] Unknown exit reason: %u\n", run->exit_reason);
            running = 0;
            break;
        }

        redraw_counter++;
        if (redraw_counter >= 500) {
            redraw_counter = 0;
            vm_devices_redraw(exit_count);
        }

        if (exit_count % 100000 == 0) {
            vm_log("[VM] Status: exits=%llu io=%llu mmio=%llu post=0x%02X\n",
                   (unsigned long long)exit_count,
                   (unsigned long long)io_count,
                   (unsigned long long)mmio_count,
                   vm_devices_get_post_code());
        }
    }

    vm_devices_redraw(exit_count);

    vm_log("[VM] Stopped after %llu exits (io=%llu mmio=%llu)\n",
           (unsigned long long)exit_count,
           (unsigned long long)io_count,
           (unsigned long long)mmio_count);

    kvm_close(kvm_fd);

    window_draw_text(win, 8, WIN_HEIGHT - 20,
                     "VM stopped. Close window to exit.", 0xFFF38BA8, 12.0f);
    draw_present();

    while (1) {
        input_keyboard_event_t ev;
        if (window_input_keyboard_poll(&ev) > 0) {
            if (ev.pressed && ev.ascii == 'q') break;
        }
        process_yield();
    }

    window_destroy(win);
    process_exit(0);
    return 0;
}
