#include "DriverModule.h"
#include "InterruptManager.h"

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
#include "smp/SMP_Main.h"
#if defined(PLATFORM_X86_64)
#include "Platform/interrupt/LAPIC.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void timer_msleep(uint32_t ms)
{
    timer_apic_sleep_ms(ms);
}

typedef struct {
    volatile uint32_t signaled;
} driver_event_impl_t;

static void *driver_map_mmio_range(uint64_t phys_addr, size_t size)
{
    if (size == 0u) {
        return NULL;
    }
#if defined(PLATFORM_ARM64)
    const uint64_t chunk_size = 1024ULL * 1024ULL;
#else
    const uint64_t chunk_size = 2ULL * 1024ULL * 1024ULL;
#endif
    uint64_t first_base = phys_addr & ~(chunk_size - 1u);
    uint64_t last_addr = phys_addr + (uint64_t)size - 1u;
    if (last_addr < phys_addr) {
        return NULL;
    }
    uint8_t *first = NULL;
    for (uint64_t base = first_base;; base += chunk_size) {
        uint8_t *mapped = (uint8_t *)map_mmio_virt(base);
        if (mapped == NULL) {
            return NULL;
        }
        if (first == NULL) {
            first = mapped;
        } else if (mapped != first + (base - first_base)) {
            return NULL;
        }
        if (base + chunk_size - 1u >= last_addr) {
            break;
        }
    }
    return first + (phys_addr - first_base);
}

static uint32_t driver_pci_get_device_count(void)
{
    return pci_get_device_count();
}

static bool driver_pci_get_device(uint32_t index,
                                  driver_pci_device_t *out_device)
{
    const pci_device_t *device = pci_get_device(index);
    if (device == NULL || out_device == NULL) {
        return false;
    }
    out_device->bus = device->bus;
    out_device->device = device->device;
    out_device->function = device->func;
    out_device->class_code = device->class_code;
    out_device->subclass = device->subclass;
    out_device->prog_if = device->prog_if;
    out_device->revision = device->revision;
    out_device->vendor_id = device->vendor_id;
    out_device->device_id = device->device_id;
    out_device->interrupt_line = device->interrupt_line;
    out_device->interrupt_pin = device->interrupt_pin;
    return true;
}

static uint32_t driver_pci_read_config(uint8_t bus, uint8_t device,
                                       uint8_t function, uint16_t offset)
{
    if (offset > 0xFCu) {
        return 0xFFFFFFFFu;
    }
    return pci_read_config(bus, device, function, (uint8_t)offset);
}

static void driver_pci_write_config(uint8_t bus, uint8_t device,
                                    uint8_t function, uint16_t offset,
                                    uint32_t value)
{
    if (offset <= 0xFCu) {
        pci_write_config(bus, device, function, (uint8_t)offset, value);
    }
}

static bool driver_pci_get_bar(uint8_t bus, uint8_t device, uint8_t function,
                               uint8_t bar_index, driver_pci_bar_t *out_bar)
{
    pci_bar_info_t bar;
    if (out_bar == NULL ||
        !pci_get_bar_info(bus, device, function, bar_index, &bar)) {
        return false;
    }
    out_bar->address = bar.address;
    out_bar->size = bar.size;
    out_bar->is_io = bar.is_io ? 1u : 0u;
    out_bar->is_64bit = bar.is_64bit ? 1u : 0u;
    out_bar->prefetchable = bar.prefetchable ? 1u : 0u;
    out_bar->reserved = 0u;
    return true;
}

static bool driver_pci_enable_bus_master(uint8_t bus, uint8_t device,
                                         uint8_t function)
{
    uint32_t command = pci_read_config(bus, device, function, 0x04u);
    if (command == 0xFFFFFFFFu) {
        return false;
    }
    command |= (1u << 2u) | (1u << 1u);
    pci_write_config(bus, device, function, 0x04u, command);
    return true;
}

static int32_t driver_pci_enable_msix(uint8_t bus, uint8_t device,
                                      uint8_t function,
                                      uint32_t requested_vectors,
                                      uint32_t *out_vectors)
{
#if !defined(PLATFORM_X86_64)
    (void)bus;
    (void)device;
    (void)function;
    (void)requested_vectors;
    (void)out_vectors;
    return -1;
#else
    if (requested_vectors == 0u || out_vectors == NULL ||
        !lapic_is_present()) {
        return -1;
    }
    int32_t cap = pci_find_capability(bus, device, function, 0x11u);
    if (cap < 0) {
        return -1;
    }
    uint32_t header = pci_read_config(bus, device, function,
                                      (uint8_t)cap);
    uint32_t available = ((header >> 16u) & 0x7FFu) + 1u;
    uint32_t count = requested_vectors < available ?
                     requested_vectors : available;
    uint32_t table = pci_read_config(bus, device, function,
                                     (uint8_t)(cap + 4));
    uint8_t bir = (uint8_t)(table & 0x7u);
    uint32_t table_offset = table & ~0x7u;
    pci_bar_info_t bar;
    if (!pci_get_bar_info(bus, device, function, bir, &bar) ||
        bar.is_io || table_offset >= bar.size ||
        (uint64_t)count * 16u > bar.size - table_offset) {
        return -1;
    }
    volatile uint32_t *entries =
        (volatile uint32_t *)driver_map_mmio_range(
            bar.address + table_offset, (size_t)count * 16u);
    if (entries == NULL) {
        return -1;
    }

    header |= (1u << 30u);
    pci_write_config(bus, device, function, (uint8_t)cap, header);
    uint32_t allocated = 0u;
    for (; allocated < count; ++allocated) {
        int32_t vector = interrupt_manager_allocate_vector();
        if (vector < 0) {
            break;
        }
        out_vectors[allocated] = (uint32_t)vector;
        uint32_t apic_id = lapic_get_id();
        entries[allocated * 4u + 3u] = 1u;
        entries[allocated * 4u + 0u] =
            0xFEE00000u | ((apic_id & 0xFFu) << 12u);
        entries[allocated * 4u + 1u] = 0u;
        entries[allocated * 4u + 2u] = (uint32_t)vector;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        entries[allocated * 4u + 3u] = 0u;
    }
    if (allocated == 0u) {
        header &= ~(1u << 30u);
        pci_write_config(bus, device, function, (uint8_t)cap, header);
        return -1;
    }
    header &= ~(1u << 30u);
    header |= (1u << 31u);
    pci_write_config(bus, device, function, (uint8_t)cap, header);
    return (int32_t)allocated;
#endif
}

static void driver_pci_disable_msix(uint8_t bus, uint8_t device,
                                    uint8_t function,
                                    const uint32_t *vectors,
                                    uint32_t vector_count)
{
    int32_t cap = pci_find_capability(bus, device, function, 0x11u);
    if (cap >= 0) {
        uint32_t header = pci_read_config(bus, device, function,
                                          (uint8_t)cap);
        header &= ~(1u << 31u);
        header |= (1u << 30u);
        pci_write_config(bus, device, function, (uint8_t)cap, header);
    }
    if (vectors != NULL) {
        for (uint32_t i = 0u; i < vector_count; ++i) {
            interrupt_manager_free_vector(vectors[i]);
        }
    }
}

static void *driver_event_create(void)
{
    driver_event_impl_t *event = malloc(sizeof(*event));
    if (event != NULL) {
        event->signaled = 0u;
    }
    return event;
}

static void driver_event_destroy(void *opaque)
{
    free(opaque);
}

static void driver_event_reset(void *opaque)
{
    driver_event_impl_t *event = (driver_event_impl_t *)opaque;
    if (event != NULL) {
        __atomic_store_n(&event->signaled, 0u, __ATOMIC_RELEASE);
    }
}

static void driver_event_signal(void *opaque)
{
    driver_event_impl_t *event = (driver_event_impl_t *)opaque;
    if (event != NULL) {
        __atomic_store_n(&event->signaled, 1u, __ATOMIC_RELEASE);
    }
}

static bool driver_event_wait(void *opaque, uint32_t timeout_ms)
{
    driver_event_impl_t *event = (driver_event_impl_t *)opaque;
    if (event == NULL) {
        return false;
    }
    uint64_t start = timer_monotonic_ns();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    for (;;) {
        if (__atomic_exchange_n(&event->signaled, 0u,
                                __ATOMIC_ACQ_REL) != 0u) {
            return true;
        }
        if (timeout_ms == 0u) {
            return false;
        }
        if (timeout_ms != UINT32_MAX &&
            timer_monotonic_ns() - start >= timeout_ns) {
            return false;
        }
        hal_cpu_pause();
    }
}

typedef struct {
    uint8_t present;
    uint8_t loaded;
    uint8_t activating;
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
        .monotonic_ns = timer_monotonic_ns,
    },
    .mem = {
        .malloc = malloc,
        .free = free,
        .dma_alloc = dma_alloc,
        .dma_free = dma_free,
        .virt_to_phys = virt_to_phys,
        .memset = memset,
        .memcpy = memcpy,
        .dma_alloc_ex = dma_alloc_ex,
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
        .map_mmio_range = driver_map_mmio_range,
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

        .cpu_memory_barrier = hal_cpu_memory_barrier,

        .arch_switch_stack = hal_arch_switch_stack,

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
    .pci = {
        .get_device_count = driver_pci_get_device_count,
        .get_device = driver_pci_get_device,
        .read_config = driver_pci_read_config,
        .write_config = driver_pci_write_config,
        .get_bar = driver_pci_get_bar,
        .find_capability = pci_find_capability,
        .enable_bus_master = driver_pci_enable_bus_master,
        .enable_msix = driver_pci_enable_msix,
        .disable_msix = driver_pci_disable_msix,
        .register_irq = interrupt_manager_register,
        .unregister_irq = interrupt_manager_unregister,
    },
    .event = {
        .create = driver_event_create,
        .destroy = driver_event_destroy,
        .reset = driver_event_reset,
        .signal = driver_event_signal,
        .wait = driver_event_wait,
    },
    .system = {
        .cpu_count = smp_get_cpu_count,
        .current_cpu = smp_get_current_cpu_id,
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
        if (state->present == 0u ||
            driver_manager_get_by_module_name(state->name) == NULL) {
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
        g_modules[i].activating = 0u;
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

static bool driver_module_prepare(module_state_t *state)
{
    if (state == NULL || state->present == 0u) {
        return false;
    }
    if (state->descriptor != NULL) {
        return true;
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

    state->descriptor = descriptor;
    state->vtable = (void *)descriptor->driver_api;
    state->kind = descriptor->kind;
    return true;
}

static bool driver_module_activate(module_state_t *state)
{
    if (!driver_module_prepare(state)) {
        return false;
    }
    if (driver_manager_get_by_module_name(state->name) != NULL) {
        return true;
    }
    if (state->activating != 0u) {
        return false;
    }

    const driver_module_descriptor_t *descriptor = state->descriptor;
    state->activating = 1u;
    for (uint32_t i = 0u;
         i < DRIVER_MAX_DEPS && descriptor->deps[i] != NULL; ++i) {
        if (!driver_module_manager_reload_by_name(descriptor->deps[i])) {
            state->activating = 0u;
            return false;
        }
    }
    bool attached = driver_manager_attach(state->name, state->kind,
                                          descriptor->driver_api);
    state->activating = 0u;
    return attached;
}

bool driver_module_init_all(void)
{
    if (g_module_count == 0u) {
        return true;
    }

    bool all_ok = true;
    for (uint32_t i = 0u; i < g_module_count; ++i) {
        if (g_modules[i].present && !driver_module_prepare(&g_modules[i])) {
            all_ok = false;
        }
    }
    driver_module_sort();

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
    state->activating = 0u;
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
    if (driver_manager_get_by_module_name(name) != NULL) {
        return true;
    }
    return driver_module_activate(state);
}
