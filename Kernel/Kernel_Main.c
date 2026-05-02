#include "Kernel_Main.h"
#include "Memory/Memory_Main.h"
#include "Paging/Paging_Main.h"
#include "SMP/SMP_Main.h"
#include "IDT/IDT_Main.h"
#include "GDT/GDT_Main.h"
#include "IO/IO_Main.h"
#include "Drivers/Module/DriverModule.h"
#include "Drivers/Module/DriverManager.h"
#include "Drivers/Module/DriverSelect.h"
#include "ELF/ELF_Loader.h"
#include "Syscall/Syscall_Main.h"
#include "Syscall/Syscall_File.h"
#include "ProcessManager/ProcessManager.h"
#include "KernelConfig.h"
#include "Sync/Spinlock.h"
#include "Timer/Timer.h"
#include "Boot/LoadBar.h"
#include "Platform/ACPI/ACPI.h"
#include "Platform/Interrupts/Interrupts.h"
#include "VFS/VFS.h"
#include <string.h>
#include "IPC/IPC_Main.h"
#include "WindowManager/WindowManager_Kernel.h"
#include "Debbuger/printf/printf.h"
#include "Debbuger/Panic/Panic.h"
#include "Network/Network_Main.h"
#include "Debbuger/Serial/Serial.h"
#include <stdio.h>
#include "VMX/VMX.h"

static BOOT_INFO g_boot_info_copy;

__attribute__((aligned(16))) static uint8_t kernel_stack[0x40000];

static uint64_t user_entry = 0;
extern void syscall_enter_user_from_frame(uint64_t saved_rsp, uint64_t user_rsp);

bool all_fs_initialize(const BOOT_INFO *boot_info) {
    const FAT32_BPB *initial_bpb = NULL;
    if (boot_info != NULL && boot_info->BootPartitionBPBValid != 0) {
        initial_bpb = &boot_info->BootPartitionBPB;
    }

    if (!driver_manager_fs_init(initial_bpb)) {
        return false;
    }
    if (!vfs_init()) {
        return false;
    }
    return true;
}

__attribute__((noreturn))
void entry_user_mode() {
    uint64_t user_rsp = process_get_current_user_rsp();
    uint64_t saved_rsp = process_get_current_saved_rsp();
    uint64_t user_cr3 = process_get_current_cr3();

    if (user_rsp == 0 || saved_rsp == 0 || user_cr3 == 0) {
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    register uint64_t rdi __asm__("rdi") = saved_rsp;
    register uint64_t rsi __asm__("rsi") = user_rsp;
    register uint64_t rax __asm__("rax") = user_cr3;

    __asm__ volatile(
        "mov %%rax, %%cr3 \n\t"
        "mov %%rdi, %%rsp \n\t"
        "jmp syscall_enter_user_from_frame"
        :: "r"(rdi), "r"(rsi), "r"(rax)
        : "memory"
    );

    __builtin_unreachable();
}

static void fb_clear(BOOT_INFO* bi, uint32_t color) {
    uint32_t* fb = (uint32_t*)bi->FrameBufferBase;
    uint32_t pixels = (uint32_t)(bi->FrameBufferSize / 4);
    
    uint32_t i = 0;
    for (; i + 8 <= pixels; i += 8) {
        fb[i]   = color; fb[i+1] = color;
        fb[i+2] = color; fb[i+3] = color;
        fb[i+4] = color; fb[i+5] = color;
        fb[i+6] = color; fb[i+7] = color;
    }
    for (; i < pixels; i++) {
        fb[i] = color;
    }
}

void kernel_boot_screen_color(uint32_t color) {
    if (g_boot_info_copy.FrameBufferBase == 0 || g_boot_info_copy.FrameBufferSize < 4) {
        return;
    }
    fb_clear(&g_boot_info_copy, color);
}

static void load_spinner_timer(uint64_t tick) {
    (void)tick;
    load_bar_update();
}

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info) {
    __asm__ volatile ("cli");
    serial_init();

    if (boot_info != NULL) {
        memcpy(&g_boot_info_copy, boot_info, sizeof(BOOT_INFO));
        boot_info = &g_boot_info_copy;
    }

    {
        uintptr_t sp = (uintptr_t)(kernel_stack + sizeof(kernel_stack));
        sp -= 16;
        sp &= ~0xFULL;
        __asm__ volatile("mov %0, %%rsp" :: "r"(sp) : "memory");
    }

    load_bar_init(boot_info);
    timer_set_callback(load_spinner_timer);

    init_gdt();
    init_idt();

    init_physical_memory(
        boot_info->MemoryMap,
        boot_info->MemoryMapSize,
        boot_info->MemoryMapDescriptorSize,
        0
    );
    init_paging();
    memory_init();
    acpi_init(boot_info);
    platform_interrupts_configure(acpi_get_info());
    syscall_init();
    smp_init();
    vmx_init();
    timer_init(60);

    __asm__ volatile ("sti");

    timer_switch_lapic();
    driver_module_manager_init(boot_info);
    driver_module_init_all();

    driver_boot_framebuffer_t boot_fb = {
        .addr = (void *)(uintptr_t)boot_info->FrameBufferBase,
        .size_bytes = (uint32_t)boot_info->FrameBufferSize,
        .width = boot_info->HorizontalResolution,
        .height = boot_info->VerticalResolution,
        .pixels_per_scan_line = boot_info->PixelsPerScanLine,
        .bytes_per_pixel = 4,
    };
    driver_select_set_boot_framebuffer(&boot_fb);
    debugger_init(boot_info);
    disk_io_init(boot_info->PartitionStartLBA, boot_info->BootDriveType);
    bool fs_ready = false;
    if (all_fs_initialize(boot_info)) {
        fs_ready = true;
    }
    bool diskless_boot = (!fs_ready && OS_CONFIG_ALLOW_DISKLESS_BOOT);
    if (!fs_ready && !diskless_boot) {
        while (1) { __asm__("hlt"); }
    }

    driver_manager_display_init();
    wm_kernel_init();
    process_manager_init();
    ipc_init();
    syscall_file_init();
    network_stack_init();

    load_bar_finish();

    if (fs_ready) {
        if (process_register_boot_process("/Userland/Userland.ELF", &user_entry) < 0) {
            while (1) { __asm__("hlt"); }
        }
    }

    if (!fs_ready) {
        while (1) { __asm__("hlt"); }
    }
    
    entry_user_mode();

    __builtin_unreachable();
}
