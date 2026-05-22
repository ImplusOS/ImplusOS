#include "kernel/boot_info.h"
#include "MemoryManagement/Memory_Main.h"
#include "mmu/Paging_Main.h"
#include "smp/SMP_Main.h"
#include "cpu/IDT_Main.h"
#include "cpu/GDT_Main.h"
#include "Platform/io/IO_Main.h"
#include "Drivers/Module/DriverModule.h"
#include "Drivers/Module/DriverManager.h"
#include "Drivers/Module/DriverSelect.h"
#include "Core/elf/ELF_Loader.h"
#include "Core/syscall/Syscall_Main.h"
#include "Core/syscall/Syscall_File.h"
#include "Core/process/ProcessManager.h"
#include "kernel/config.h"
#include "Core/sync/Spinlock.h"
#include "Core/timer/Timer.h"
#include "Boot/LoadBar.h"
#include "Platform/acpi/ACPI.h"
#include "Platform/interrupt/Interrupts.h"
#include "Core/vfs/VFS.h"
#include <string.h>
#include "IPC/IPC_Main.h"
#include "Core/window/WindowManager_Kernel.h"
#include "Debug/printf/printf.h"
#include "Debug/panic/Panic.h"
#include "Network/network_main.h"
#include "Debug/serial/Serial.h"
#include <stdio.h>
#include "virt/VMX.h"
#include "interfaces/arch_ops.h"

static BOOT_INFO g_boot_info_copy;

__attribute__((aligned(16))) static uint8_t kernel_stack[0x40000];

static uint64_t user_entry = 0;
extern const arch_ops_t *arch_ops_get(void);

bool all_fs_initialize(const BOOT_INFO *boot_info) {
    const FAT32_BPB *initial_bpb = NULL;
    if (boot_info != NULL && boot_info->BootPartitionBPBValid != 0) {
        initial_bpb = &boot_info->BootPartitionBPB;
    }

    if (!fat32_init(initial_bpb)) {
        return false;
    }
    if (!vfs_init()) {
        return false;
    }
    return true;
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

    kernel_panic_init(boot_info);

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
        (void *)boot_info->MemoryMap,
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
    timer_init(60);

    vmx_init();

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
        .bytes_per_pixel = 4
    };
    driver_select_set_boot_framebuffer(&boot_fb);
    debugger_init(boot_info);
    
    disk_io_init(boot_info->PartitionStartLBA, boot_info->BootDriveType);

    bool fs_ready = false;
    if (all_fs_initialize(boot_info)) {
        serial_write_string("Init success");
        fs_ready = true;
    }

    if (!fs_ready) {
        kernel_panic("Filesystem initialization failed and diskless boot not enabled", "kernel_main");
    }

    driver_manager_display_init();
    wm_kernel_init();
    process_manager_init();
    ipc_init();
    syscall_file_init();
    network_stack_init();
    load_bar_finish();

    const arch_ops_t *ops = arch_ops_get();

    if (fs_ready) {
        if (process_register_boot_process("/Userland/Userland.ELF", &user_entry) < 0) {
            while (1) { __asm__("hlt"); }
        }
    }

    uint64_t user_rsp = process_get_current_user_rsp();
    uint64_t saved_rsp = process_get_current_saved_rsp();
    uint64_t user_cr3 = process_get_current_cr3();

    if (user_rsp == 0 || saved_rsp == 0 || user_cr3 == 0) {
        while (1) { __asm__ volatile("cli; hlt"); }
    }

    if (ops) {
        ops->enter_user_mode(saved_rsp, user_rsp, user_cr3);
    }

    for(;;) { __asm__("hlt"); }
}
