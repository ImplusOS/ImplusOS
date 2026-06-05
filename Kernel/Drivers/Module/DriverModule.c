#include "DriverModule.h"

#include "Drivers/Client/PCI/PCI_Main.h"
#include "Drivers/Client/FileSystem/FAT32/FAT32_Main.h"
#include "Drivers/Server/Display/Display_Driver.h"
#include "Core/elf/ELF_Loader.h"
#include "Platform/io/IO_Main.h"
#include "MemoryManagement/DMA_Memory.h"
#include "MemoryManagement/Memory_Main.h"
#include "mmu/Paging_Main.h"
#include "Debug/serial/Serial.h"
#include "Core/timer/Timer.h"
#include "Debug/printf/printf.h"
#include "Network/network_main.h"
#include "interfaces/hal_cpu.h"
#include "interfaces/hal_io.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void timer_msleep(uint32_t ms)
{
    timer_apic_sleep_ms(ms);
}

typedef struct {
    uint8_t present;
    uint8_t loaded;
    char name[LOADED_FILE_NAME_MAX];
    const void *file_data;
    uint64_t file_size;
    uint64_t entry;
    void *vtable;
    const driver_module_descriptor_t *descriptor;
    void *load_base;
    uint32_t load_page_count;
    device_type_t kind;
} module_state_t;

static module_state_t g_modules[MAX_LOADED_FILES];
static uint32_t g_module_count = 0;

static const driver_binary_t g_driver_api = {
    .version_major = DRIVER_API_VERSION_MAJOR,
    .version_minor = DRIVER_API_VERSION_MINOR,
    .timer = {
        .msleep = timer_msleep,
        .hz = timer_hz,
        .ticks = timer_ticks,
    },
    .mem = {
        .malloc = malloc,
        .free = free,
        .dma_alloc = dma_alloc,
        .dma_free = dma_free,
        .virt_to_phys = virt_to_phys,
        .memset = memset,
        .memcpy = memcpy,
    },
    .io = {
        .inb = inb,
        .outb = outb,
        .inl = inl,
        .outl = outl,
    },
    .hw = {
        .disk_read = disk_read,
        .disk_write = disk_write,
        .disk_get_partition_lba = disk_get_partition_lba,
        .pci_read_config = pci_read_config,
        .pci_write_config = pci_write_config,
        .map_mmio_virt = map_mmio_virt,
    },
    .dbg = {
        .write_char = serial_write_char,
        .write_string = serial_write_string,
        .write_uint32 = serial_write_uint32,
    },
    .hal = {
        .cpu_halt = hal_cpu_halt,
        .cpu_pause = hal_cpu_pause,
        .cpu_enable_interrupts = hal_cpu_enable_interrupts,
        .cpu_disable_interrupts = hal_cpu_disable_interrupts,
        .cpu_save_interrupts = hal_cpu_save_interrupts,
        .cpu_restore_interrupts = hal_cpu_restore_interrupts,
        .mmu_invalidate_tlb = hal_mmu_invalidate_tlb,
        .cpu_read_cr = hal_cpu_read_cr,
        .cpu_write_cr = hal_cpu_write_cr,
        .cpu_memory_barrier = hal_cpu_memory_barrier,
        .io_delay = hal_io_delay,
        .cpu_read_msr = hal_cpu_read_msr,
        .cpu_write_msr = hal_cpu_write_msr,
        .cpu_get_id = hal_cpu_get_id,
        .cpu_get_gdt_ptr = hal_cpu_get_gdt_ptr,
        .cpu_invalidate_caches = hal_cpu_invalidate_caches,
        .arch_switch_stack = hal_arch_switch_stack,
        .cpu_get_current_el = hal_cpu_get_current_el,
        .cpu_set_vbar = hal_cpu_set_vbar,
        .cpu_read_fs_base = hal_cpu_read_fs_base,
        .cpu_write_fs_base = hal_cpu_write_fs_base,
        .cpu_save_fpu = hal_cpu_save_fpu,
        .cpu_restore_fpu = hal_cpu_restore_fpu,

        .io_out8 = hal_io_out8,
        .io_in8 = hal_io_in8,
        .io_out16 = hal_io_out16,
        .io_in16 = hal_io_in16,
        .io_out32 = hal_io_out32,
        .io_in32 = hal_io_in32,
        .io_outsw = hal_io_outsw,
    },
    .timer_msleep = timer_msleep,
    .timer_hz = timer_hz,
    .timer_ticks = timer_ticks,
    .malloc = malloc,
    .free = free,
    .dma_alloc = dma_alloc,
    .dma_free = dma_free,
    .virt_to_phys = virt_to_phys,
    .memset = memset,
    .memcpy = memcpy,
    .inb = inb,
    .outb = outb,
    .inl = inl,
    .outl = outl,
    .disk_read = disk_read,
    .disk_write = disk_write,
    .disk_get_partition_lba = disk_get_partition_lba,
    .pci_read_config = pci_read_config,
    .pci_write_config = pci_write_config,
    .map_mmio_virt = map_mmio_virt,
    .serial_write_char = serial_write_char,
    .serial_write_string = serial_write_string,
    .serial_write_uint32 = serial_write_uint32,
};

static module_state_t *driver_module_find_state(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    for (uint32_t i = 0; i < g_module_count; ++i) {
        if (g_modules[i].present != 0u && strcasecmp(g_modules[i].name, name) == 0) {
            return &g_modules[i];
        }
    }

    return NULL;
}

static bool driver_module_has_loaded_dependents(const char *name)
{
    for (uint32_t i = 0; i < g_module_count; ++i) {
        const module_state_t *state = &g_modules[i];
        if (state->present == 0u || state->loaded == 0u) {
            continue;
        }

        if (state->descriptor == NULL) {
            continue;
        }
        for (uint32_t dep_index = 0; dep_index < DRIVER_MAX_DEPS; ++dep_index) {
            const char *dependency = state->descriptor->deps[dep_index];
            if (dependency == NULL) {
                break;
            }
            if (strcasecmp(dependency, name) == 0) {
                return true;
            }
        }
    }

    return false;
}

void driver_module_manager_init(const BOOT_INFO *boot_info)
{
    driver_manager_init();
    g_module_count = 0;

    for (uint32_t i = 0; i < MAX_LOADED_FILES; ++i) {
        g_modules[i].present = 0u;
        g_modules[i].loaded = 0u;
        g_modules[i].file_data = NULL;
        g_modules[i].file_size = 0u;
        g_modules[i].entry = 0u;
        g_modules[i].vtable = NULL;
        g_modules[i].descriptor = NULL;
        g_modules[i].load_base = NULL;
        g_modules[i].load_page_count = 0u;
        g_modules[i].kind = DEVICE_TYPE_UNKNOWN;
        g_modules[i].name[0] = '\0';
    }

    if (boot_info == NULL) {
        return;
    }

    uint64_t nfiles = (uint64_t)boot_info->LoadedFileCount;
    if (nfiles > (uint64_t)MAX_LOADED_FILES) {
        nfiles = (uint64_t)MAX_LOADED_FILES;
    }

    for (uint64_t i = 0; i < nfiles; ++i) {
        const LOADED_FILE *file = &boot_info->LoadedFiles[i];
        if (file->PhysAddr == 0u || file->Size == 0u || file->Name[0] == '\0') {
            continue;
        }
        
        const void *src_ptr = (const void *)(uintptr_t)file->PhysAddr;
        if (file->PhysAddr >= (1ULL << 32)) {
            src_ptr = map_mmio_virt(file->PhysAddr);
            if (src_ptr == NULL) {
                continue;
            }
        }

        void *kernel_buf = malloc((size_t)file->Size);
        if (kernel_buf == NULL) {
            continue;
        }
        memcpy(kernel_buf, src_ptr, (size_t)file->Size);

        module_state_t *state = &g_modules[g_module_count];
        state->present = 1u;
        state->file_data = kernel_buf;
        state->file_size = file->Size;
        state->kind = DEVICE_TYPE_UNKNOWN;
        strncpy(state->name, file->Name, LOADED_FILE_NAME_MAX - 1u);
        state->name[LOADED_FILE_NAME_MAX - 1u] = '\0';
        ++g_module_count;
    }
}

const driver_binary_t *driver_module_manager_kernel_api(void)
{
    return &g_driver_api;
}

bool driver_module_manager_load_by_name(const char *name,
                                        uint64_t max_file_size,
                                        uint64_t max_image_size,
                                        uint64_t *entry_out)
{
    if (entry_out == NULL || name == NULL || name[0] == '\0' ||
        max_file_size == 0u || max_image_size == 0u) {
        return false;
    }

    module_state_t *state = driver_module_find_state(name);
    if (state == NULL || state->file_data == NULL || state->file_size == 0u) {
        return false;
    }

    if (state->loaded != 0u) {
        *entry_out = state->entry;
        return true;
    }

    elf_module_load_policy_t policy = {
        .max_file_size = max_file_size,
        .max_image_size = max_image_size,
        .max_relocation_count = 4096u,
    };
    elf_loaded_module_t module_image;

    if (!elf_loader_load_module_image_from_memory(state->file_data,
                                                  state->file_size,
                                                  &policy,
                                                  &module_image)) {
        return false;
    }

    state->loaded = 1u;
    state->entry = module_image.entry;
    state->load_base = module_image.load_base;
    state->load_page_count = module_image.page_count;
    *entry_out = module_image.entry;
    return true;
}

void *driver_module_manager_get_driver(const char *name)
{
    return (void *)driver_manager_get_by_module_name(name);
}

#define DRIVER_MODULE_MAX_FILE_SIZE  (2ULL * 1024ULL * 1024ULL)
#define DRIVER_MODULE_MAX_IMAGE_SIZE (4ULL * 1024ULL * 1024ULL)

static uint32_t get_driver_priority(const module_state_t *state)
{
    if (state != NULL &&
        state->descriptor != NULL &&
        state->descriptor->magic == DRIVER_DESCRIPTOR_MAGIC) {
        return state->descriptor->load_priority;
    }
    return 50u;
}

static void driver_module_sort(void)
{
    for (uint32_t i = 1; i < g_module_count; ++i) {
        module_state_t key = g_modules[i];
        uint32_t j = i;

        while (j > 0u &&
               get_driver_priority(&g_modules[j - 1u]) > get_driver_priority(&key)) {
            g_modules[j] = g_modules[j - 1u];
            --j;
        }

        g_modules[j] = key;
    }
}

static bool driver_module_activate(module_state_t *state)
{
    if (state == NULL || state->present == 0u) {
        return false;
    }

    uint64_t entry = 0;
    if (!driver_module_manager_load_by_name(state->name,
                                            DRIVER_MODULE_MAX_FILE_SIZE,
                                            DRIVER_MODULE_MAX_IMAGE_SIZE,
                                            &entry)) {
        return false;
    }

    driver_module_init_fn_t init_fn = (driver_module_init_fn_t)(uintptr_t)entry;
    const driver_module_descriptor_t *descriptor = init_fn(&g_driver_api);
    if (descriptor == NULL ||
        descriptor->magic != DRIVER_DESCRIPTOR_MAGIC ||
        descriptor->version < DRIVER_DESCRIPTOR_VERSION ||
        descriptor->driver_api == NULL ||
        descriptor->kind == DEVICE_TYPE_UNKNOWN) {
        return false;
    }

    for (uint32_t i = 0; i < DRIVER_MAX_DEPS && descriptor->deps[i] != NULL; ++i) {
        if (!driver_module_manager_reload_by_name(descriptor->deps[i])) {
            return false;
        }
    }

    state->descriptor = descriptor;
    state->vtable = (void *)descriptor->driver_api;
    state->kind = descriptor->kind;
    return driver_manager_attach(state->name, state->kind, descriptor->driver_api);
}

bool driver_module_init_all(void)
{
    if (g_module_count == 0u) {
        return true;
    }

    driver_module_sort();

    bool all_ok = true;
    for (uint32_t i = 0; i < g_module_count; ++i) {
        if (!g_modules[i].present) {
            continue;
        }
        if (!driver_module_activate(&g_modules[i])) {
            all_ok = false;
        }
    }
    return all_ok;
}

bool driver_module_manager_unload_by_name(const char *name)
{
    module_state_t *state = driver_module_find_state(name);
    if (state == NULL) {
        return false;
    }

    if (state->loaded == 0u) {
        return driver_manager_detach(name);
    }

    if (driver_module_has_loaded_dependents(name)) {
        return false;
    }

    (void)driver_manager_detach(name);

    if (state->descriptor != NULL && state->descriptor->shutdown != NULL) {
        state->descriptor->shutdown();
    }
    if (state->load_base != NULL && state->load_page_count != 0u) {
        free_contiguous_pages(state->load_base, state->load_page_count);
    }

    state->loaded = 0u;
    state->entry = 0u;
    state->vtable = NULL;
    state->descriptor = NULL;
    state->load_base = NULL;
    state->load_page_count = 0u;
    return true;
}

bool driver_module_manager_reload_by_name(const char *name)
{
    module_state_t *state = driver_module_find_state(name);
    if (state == NULL) {
        return false;
    }
    if (state->loaded != 0u) {
        return true;
    }
    return driver_module_activate(state);
}
