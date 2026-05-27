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
#include "Drivers/Client/FileSystem/FAT32/FAT32_Main.h"
#include "Drivers/Client/FileSystem/FAT32/FAT32_VFS_Adapter.h"
#include "Drivers/Client/FileSystem/ISO9660/ISO9660_Main.h"
#include "Drivers/Client/FileSystem/ISO9660/ISO9660_VFS_Adapter.h"
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

static uint32_t *g_fb_snapshot = NULL;
static uint32_t g_fb_snapshot_pixels = 0;

__attribute__((aligned(16))) static uint8_t kernel_stack[0x40000];

static uint64_t user_entry = 0;
extern const arch_ops_t *arch_ops_get(void);

#include "Debug/serial/Serial.h"

bool all_fs_initialize(const BOOT_INFO *boot_info) {
    if (!vfs_init()) {
        return false;
    }
    
    if (!fat32_init(&boot_info->BootPartitionBPB)) {
        return false;
    }
    
    if (!iso9660_init()) {
        return false;
    }
    
    vfs_mount("", fat32_vfs_get_driver());
    vfs_mount("", iso9660_vfs_get_driver());
    
    vfs_set_default_fs("iso9660");
    
    return true;
}

static inline uint32_t alpha_blend(uint32_t dst, uint32_t src) {
    uint8_t a  = (src >> 24) & 0xFF;

    if (a == 255) {
        return src;
    }

    if (a == 0) {
        return dst;
    }

    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8)  & 0xFF;
    uint8_t sb = (src >> 0)  & 0xFF;

    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8)  & 0xFF;
    uint8_t db = (dst >> 0)  & 0xFF;

    uint8_t r = (uint8_t)((sr * a + dr * (255 - a)) / 255);
    uint8_t g = (uint8_t)((sg * a + dg * (255 - a)) / 255);
    uint8_t b = (uint8_t)((sb * a + db * (255 - a)) / 255);

    return
        (0xFF << 24) |
        (r << 16) |
        (g << 8) |
        b;
}

static bool fb_snapshot_create(BOOT_INFO *bi) {
    if (!bi || !bi->FrameBufferBase || bi->FrameBufferSize == 0) {
        return false;
    }

    g_fb_snapshot_pixels = (uint32_t)(bi->FrameBufferSize / 4);

    g_fb_snapshot = malloc(bi->FrameBufferSize);

    if (!g_fb_snapshot) {
        return false;
    }

    memcpy(
        g_fb_snapshot,
        (void *)bi->FrameBufferBase,
        bi->FrameBufferSize
    );

    return true;
}

static void fb_clear(BOOT_INFO* bi, uint32_t color) {
    if (!g_fb_snapshot) {
        return;
    }

    uint32_t* fb = (uint32_t*)bi->FrameBufferBase;
    uint32_t pixels = (uint32_t)(bi->FrameBufferSize / 4);

    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = alpha_blend(g_fb_snapshot[i], color);
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
    timer_init(60);
    syscall_init();
    smp_init();

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
        fs_ready = true;
    }

    iso9660_list_root_files();

    if (!fs_ready) {
        kernel_panic("Filesystem initialization failed and diskless boot not enabled", "kernel_main");
    }

    driver_manager_display_init();
    wm_kernel_init();
    process_manager_init();
    ipc_init();
    syscall_file_init();
    network_stack_init();
    fb_snapshot_create(boot_info);
    load_bar_finish();

    for (int i = 0; i <= 10; i++) {
        timer_apic_sleep_ms(1);
        uint8_t alpha = (uint8_t)(i * 255 / 10);
        uint32_t color =
            (alpha << 24) |
            0x000000;
        kernel_boot_screen_color(color);
    }

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
