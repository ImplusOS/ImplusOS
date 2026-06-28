#include "kernel/boot_info.h"
#include "kernel/platform.h"
#include "MemoryManagement/Memory_Main.h"
#include "mmu/Paging_Main.h"
#include "smp/SMP_Main.h"
#ifdef PLATFORM_X86_64
#include "cpu/IDT_Main.h"
#include "cpu/GDT_Main.h"
#include "virt/VMX.h"
#endif
#include "Platform/io/IO_Main.h"
#include "Drivers/Module/DriverModule.h"
#include "Drivers/Module/BlockManager.h"
#include "Drivers/Module/AudioManager.h"
#include "Drivers/Module/InputManager.h"
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
#include "interfaces/arch_ops.h"
#include "interfaces/timer_hal.h"

static BOOT_INFO g_boot_info_copy;

static uint32_t *g_fb_snapshot = NULL;
static uint32_t g_fb_snapshot_pixels = 0;
static uint64_t g_boot_framebuffer_phys_base = 0;
static uint64_t g_boot_framebuffer_phys_size = 0;

__attribute__((aligned(16))) static uint8_t kernel_stack[0x40000];

static uint64_t user_entry = 0;
extern const arch_ops_t *arch_ops_get(void);

static const char *kernel_boot_drive_type_name(uint32_t type)
{
    switch (type) {
        case BOOT_DRIVE_TYPE_IDE:
            return "IDE";
        case BOOT_DRIVE_TYPE_USB:
            return "USB";
        case BOOT_DRIVE_TYPE_AHCI:
            return "AHCI";
        case BOOT_DRIVE_TYPE_NVME:
            return "NVMe";
        case BOOT_DRIVE_TYPE_VIRTIO:
            return "VirtIO";
        case BOOT_DRIVE_TYPE_UNKNOWN:
        default:
            return "unknown";
    }
}

const BOOT_INFO *kernel_get_boot_info(void)
{
    return &g_boot_info_copy;
}

static inline void kernel_arch_halt(void)
{
    hal_cpu_halt();
}

static inline void kernel_arch_switch_stack(uintptr_t sp)
{
    hal_arch_switch_stack(sp);
}

#define BOOT_FRAMEBUFFER_MAP_GRANULE (2ULL * 1024ULL * 1024ULL)

static uint64_t kernel_map_boot_framebuffer_range(uint64_t phys_base,
                                                  uint64_t size)
{
    if (phys_base == 0 || size == 0) {
        return phys_base;
    }

    uint64_t base = phys_base & ~(BOOT_FRAMEBUFFER_MAP_GRANULE - 1ULL);
    uint64_t offset = phys_base - base;
    uint64_t span = offset + size;
    if (span < offset) {
        return 0;
    }

    uint64_t chunks = (span + BOOT_FRAMEBUFFER_MAP_GRANULE - 1ULL) /
                      BOOT_FRAMEBUFFER_MAP_GRANULE;
    uint64_t first_virt = 0;

    for (uint64_t i = 0; i < chunks; ++i) {
        uint64_t chunk_phys = base + (i * BOOT_FRAMEBUFFER_MAP_GRANULE);
        void *mapped = map_mmio_virt(chunk_phys);
        if (mapped == NULL) {
            return 0;
        }

        uint64_t chunk_virt = (uint64_t)(uintptr_t)mapped;
        if (i == 0) {
            first_virt = chunk_virt;
        } else if (chunk_virt !=
                   first_virt + (i * BOOT_FRAMEBUFFER_MAP_GRANULE)) {
            return 0;
        }
    }

    return first_virt + offset;
}

static void kernel_rebind_boot_framebuffer_after_paging(BOOT_INFO *boot_info)
{
    if (boot_info == NULL ||
        g_boot_framebuffer_phys_base == 0 ||
        g_boot_framebuffer_phys_size == 0) {
        return;
    }

    uint64_t virt_base = kernel_map_boot_framebuffer_range(
        g_boot_framebuffer_phys_base,
        g_boot_framebuffer_phys_size
    );
    if (virt_base == 0) {
        return;
    }

    boot_info->FrameBufferBase = virt_base;
    g_boot_info_copy.FrameBufferBase = virt_base;

    debugger_init(boot_info);
    kernel_panic_init(boot_info);
    load_bar_init(boot_info);
    serial_set_screen_mirror(NULL);
}

bool all_fs_initialize(void)
{
    if (!vfs_init()) {
        return false;
    }
    bool iso_ok = iso9660_init();
    bool fat_ok = fat32_init();

    if (!iso_ok && !fat_ok) {
        return false;
    }

    if (fat_ok) {
        const vfs_driver_t *fat_drv = fat32_vfs_get_driver();
        vfs_mount("", fat_drv);
    }
    if (iso_ok) {
        const vfs_driver_t *iso_drv = iso9660_vfs_get_driver();
        vfs_mount("", iso_drv);
        vfs_set_default_fs("iso9660");
    }

    return true;
}

static inline uint32_t alpha_blend(uint32_t dst, uint32_t src)
{
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

static bool fb_snapshot_create(BOOT_INFO *bi)
{
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

static void fb_clear(BOOT_INFO *bi, uint32_t color)
{
    if (!g_fb_snapshot) {
        return;
    }

    uint32_t *fb = (uint32_t *)bi->FrameBufferBase;
    uint32_t pixels = (uint32_t)(bi->FrameBufferSize / 4);

    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = alpha_blend(g_fb_snapshot[i], color);
    }
}

void kernel_boot_screen_color(uint32_t color)
{
    if (g_boot_info_copy.FrameBufferBase == 0 || g_boot_info_copy.FrameBufferSize < 4) {
        return;
    }
    fb_clear(&g_boot_info_copy, color);
}

static void load_spinner_timer(uint64_t tick)
{
    load_bar_tick(tick);
    load_bar_update();
}

#ifdef PLATFORM_X86_64
extern const timer_hal_t lapic_timer_hal;
#elif defined(PLATFORM_ARM64)
extern const timer_hal_t generic_timer_hal;
#endif

static void kernel_main_after_stack_switch(BOOT_INFO *boot_info)
{
    load_bar_init(boot_info);
    timer_set_callback(load_spinner_timer);

#ifdef PLATFORM_X86_64
    {
        const arch_ops_t *ops = arch_ops_get();
        if (ops && ops->init_cpu_tables) {
            ops->init_cpu_tables();
        } else {
            init_gdt();
            init_idt();
        }
    }
#elif defined(PLATFORM_ARM64)
    {
        const arch_ops_t *ops = arch_ops_get();
        if (ops && ops->init_cpu_tables) {
            ops->init_cpu_tables();
        }
    }
#endif

    init_physical_memory(
        (void *)boot_info->MemoryMap,
        boot_info->MemoryMapSize,
        boot_info->MemoryMapDescriptorSize,
        0
    );
    physical_memory_reserve_region(
        boot_info->FrameBufferBase,
        boot_info->FrameBufferSize
    );
    serial_set_screen_mirror(NULL);
    init_paging();
    kernel_rebind_boot_framebuffer_after_paging(boot_info);
    memory_init();
    acpi_init(boot_info);
    platform_interrupts_configure(acpi_get_info());

    const timer_hal_t *timer = NULL;
#ifdef PLATFORM_X86_64
    timer = &lapic_timer_hal;
#elif defined(PLATFORM_ARM64)
    timer = &generic_timer_hal;
#endif
    timer_init(timer);
    syscall_init();
    smp_init();

#ifdef PLATFORM_X86_64
    {
        const arch_ops_t *ops = arch_ops_get();
        if (ops && ops->virtualization_init) {
            int vmx_status = ops->virtualization_init();
        }
    }
#endif

    {
        const arch_ops_t *ops = arch_ops_get();
        if (ops && ops->enable_interrupts) {
            ops->enable_interrupts();
        }
    }
#if defined(PLATFORM_X86_64) || defined(PLATFORM_ARM64)
    timer_switch_lapic();
#endif
    timer_start_clock();
    driver_module_manager_init(boot_info);
    uint64_t driver_init_irq_flags = irq_save_disable();
    driver_module_init_all();
    irq_restore(driver_init_irq_flags);
    audio_manager_init();

    driver_boot_framebuffer_t boot_fb = {
        .addr = (void *)(uintptr_t)g_boot_framebuffer_phys_base,
        .size_bytes = (uint32_t)g_boot_framebuffer_phys_size,
        .width = boot_info->HorizontalResolution,
        .height = boot_info->VerticalResolution,
        .pixels_per_scan_line = boot_info->PixelsPerScanLine,
        .bytes_per_pixel = 4
    };
    driver_select_set_boot_framebuffer(&boot_fb);
    input_manager_init();
    block_manager_set_boot_identity(boot_info);
    bool disk_ok = disk_io_init(boot_info->PartitionStartLBA, boot_info->BootDriveType);
    if (!disk_ok) {
        kernel_panic("Disk Protocol initialization failed", "kernel_main");
    }

    bool fs_ready = false;
    if (all_fs_initialize()) {
        fs_ready = true;
    }

    if (!fs_ready) {
        kernel_panic("Filesystem initialization failed and diskless boot not enabled", "kernel_main");
    }

    serial_enable_file_logging("/Kernel.log");
    bool display_ready = driver_manager_display_init();
    bool debug_display_ready = debugger_display_init();
    serial_set_screen_mirror(NULL);
    wm_kernel_init();
    process_manager_init();
    ipc_init();
    syscall_file_init();
    network_stack_init();
    bool fb_snapshot_ok = fb_snapshot_create(boot_info);
    load_bar_finish();
    for (int i = 0; i <= 10; i++) {
        timer_apic_sleep_ms(1);
        uint8_t alpha = (uint8_t)(i * 255 / 10);
        uint32_t color =
            (alpha << 24) |
            0x000000;
        kernel_boot_screen_color(color);
    }

    if (fs_ready) {
        static const uint8_t userland_elf_magic[4] = {0x7Fu, 'E', 'L', 'F'};
        if (!vfs_set_default_fs_for_file("/Userland/Userland.ELF",
                                         64u,
                                         userland_elf_magic,
                                         sizeof(userland_elf_magic))) {
            vfs_init();
            if (all_fs_initialize() &&
                vfs_set_default_fs_for_file("/Userland/Userland.ELF",
                                           64u,
                                           userland_elf_magic,
                                           sizeof(userland_elf_magic))) {
                serial_write_string("[kernel:init] retry FS init: SUCCESS\n");
            } else {
                kernel_panic("Userland ELF not found or unreadable.", "kernel_main");
            }
        }

        if (process_register_boot_process("/Userland/Userland.ELF", &user_entry) < 0) {
            const char *elf_err = elf_loader_last_error();
            char panic_msg[128];
            if (elf_err) {
                snprintf(panic_msg, sizeof(panic_msg),
                         "Failed to start Userland (ELF: %s)", elf_err);
            } else {
                snprintf(panic_msg, sizeof(panic_msg),
                         "Failed to start Userland.");
            }
            kernel_panic(panic_msg, "kernel_main");
        }
    }

    timer_start_services();
    serial_set_screen_mirror(debug_putchar);

    uint64_t user_rsp = process_get_current_user_rsp();
    uint64_t saved_rsp = process_get_current_saved_rsp();
    uint64_t user_cr3 = process_get_current_cr3();

    if (user_rsp == 0 || saved_rsp == 0 || user_cr3 == 0 || user_entry == 0) {
        const arch_ops_t *ops = arch_ops_get();
        if (ops && ops->disable_interrupts) {
            ops->disable_interrupts();
        }
        kernel_panic("Failed get RSP and CR3", "kernel_main");
    }

    {
        const arch_ops_t *ops = arch_ops_get();
        if (ops) {
            ops->enter_user_mode(saved_rsp, user_rsp, user_cr3);
        }
    }

    for (;;) { kernel_arch_halt(); }
}

__attribute__((noreturn))
void kernel_main(BOOT_INFO *boot_info)
{
    const arch_ops_t *ops = arch_ops_get();
    if (ops && ops->disable_interrupts) {
        ops->disable_interrupts();
    }
    if (ops && ops->early_init) {
        ops->early_init();
    }

    debugger_init(boot_info);

    serial_init();

    if (boot_info != NULL) {
        memcpy(&g_boot_info_copy, boot_info, sizeof(BOOT_INFO));
        boot_info = &g_boot_info_copy;
        g_boot_framebuffer_phys_base = boot_info->FrameBufferBase;
        g_boot_framebuffer_phys_size = boot_info->FrameBufferSize;
    }

    kernel_panic_init(boot_info);

    uintptr_t sp = (uintptr_t)(kernel_stack + sizeof(kernel_stack));
    sp &= ~0xFULL;

    #ifdef PLATFORM_X86_64
    {
        hal_arch_switch_stack_and_jump(sp, kernel_main_after_stack_switch, boot_info);
    }
    #elif defined(PLATFORM_ARM64)
        {
            hal_arch_switch_stack_and_jump(sp, kernel_main_after_stack_switch, boot_info);
        }
    #endif

    __builtin_unreachable();
}
