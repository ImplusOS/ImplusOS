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
#include "Network/network_main.h"

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
    driver_manager_kind_t kind;
} module_state_t;

static module_state_t g_modules[MAX_LOADED_FILES];
static uint32_t g_module_count = 0;

static const driver_binary_t g_driver_api = {
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
    .pci_read_config = pci_read_config,
    .pci_write_config = pci_write_config,
    .map_mmio_virt = map_mmio_virt,
    .serial_write_char = serial_write_char,
    .serial_write_string = serial_write_string,
    .serial_write_uint32 = serial_write_uint32,
};

static bool str_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return false;
        }
        ++a;
        ++b;
    }

    return (*a == '\0' && *b == '\0');
}

static void str_copy(char *dst, uint32_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    uint32_t i = 0;
    if (src != NULL) {
        while (src[i] != '\0' && i + 1u < dst_size) {
            dst[i] = src[i];
            ++i;
        }
    }

    dst[i] = '\0';
}

static driver_manager_kind_t driver_module_detect_kind(const char *name)
{
    if (name == NULL) {
        return DRIVER_MANAGER_KIND_UNKNOWN;
    }

    if (strcmp(name, "PCI_Driver.ELF") == 0) {
        return DRIVER_MANAGER_KIND_PCI;
    }
    if (strcmp(name, "FAT32_Driver.ELF") == 0) {
        return DRIVER_MANAGER_KIND_FAT32;
    }
    if (strcmp(name, "VirtIO_Driver.ELF") == 0 ||
        strcmp(name, "ImplusOS_Generic_Display_Driver.ELF") == 0) {
        return DRIVER_MANAGER_KIND_DISPLAY;
    }
    if (strcmp(name, "PS2_Driver.ELF") == 0) {
        return DRIVER_MANAGER_KIND_INPUT;
    }
    if (strcmp(name, "USB_Driver.ELF") == 0) {
        return DRIVER_MANAGER_KIND_USB;
    }

    return DRIVER_MANAGER_KIND_UNKNOWN;
}

static module_state_t *driver_module_find_state(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }

    for (uint32_t i = 0; i < g_module_count; ++i) {
        if (g_modules[i].present != 0u && str_equal(g_modules[i].name, name)) {
            return &g_modules[i];
        }
    }

    return NULL;
}

static uint32_t driver_module_dependency_count(const char *name)
{
    if (name == NULL) {
        return 0u;
    }
    if (strcmp(name, "VirtIO_Driver.ELF") == 0 ||
        strcmp(name, "USB_Driver.ELF") == 0) {
        return 1u;
    }
    return 0u;
}

static const char *driver_module_dependency_name(const char *name, uint32_t index)
{
    if (index != 0u || name == NULL) {
        return NULL;
    }
    if (strcmp(name, "VirtIO_Driver.ELF") == 0 ||
        strcmp(name, "USB_Driver.ELF") == 0) {
        return "PCI_Driver.ELF";
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

        uint32_t dep_count = driver_module_dependency_count(state->name);
        for (uint32_t dep_index = 0; dep_index < dep_count; ++dep_index) {
            const char *dependency = driver_module_dependency_name(state->name, dep_index);
            if (dependency != NULL && strcmp(dependency, name) == 0) {
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
        g_modules[i].kind = DRIVER_MANAGER_KIND_UNKNOWN;
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
        state->kind = driver_module_detect_kind(file->Name);
        str_copy(state->name, LOADED_FILE_NAME_MAX, file->Name);
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

typedef struct {
    const char *name;
    uint32_t priority;
} driver_priority_t;

static const driver_priority_t g_driver_priority[] = {
    {"PCI_Driver.ELF", 10u},
    {"VirtIO_Driver.ELF", 20u},
    {"USB_Driver.ELF", 30u},
    {"PS2_Driver.ELF", 40u},
    {"FAT32_Driver.ELF", 100u},
};

static uint32_t get_driver_priority(const char *name)
{
    for (uint32_t i = 0; i < sizeof(g_driver_priority) / sizeof(g_driver_priority[0]); ++i) {
        if (strcmp(name, g_driver_priority[i].name) == 0) {
            return g_driver_priority[i].priority;
        }
    }
    return 50u;
}

static void driver_module_sort(void)
{
    for (uint32_t i = 1; i < g_module_count; ++i) {
        module_state_t key = g_modules[i];
        uint32_t j = i;

        while (j > 0u &&
               get_driver_priority(g_modules[j - 1u].name) > get_driver_priority(key.name)) {
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

    for (uint32_t i = 0; i < driver_module_dependency_count(state->name); ++i) {
        const char *dependency = driver_module_dependency_name(state->name, i);
        if (dependency == NULL || !driver_module_manager_reload_by_name(dependency)) {
            return false;
        }
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
    if (descriptor == NULL || descriptor->driver_api == NULL) {
        return false;
    }

    state->descriptor = descriptor;
    state->vtable = (void *)descriptor->driver_api;
    state->kind = driver_module_detect_kind(state->name);
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
