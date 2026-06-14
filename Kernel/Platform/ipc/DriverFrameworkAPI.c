#include "../../../Userland/DriverFramework/API/DriverFrameworkAPI.h"

#include "kernel/status.h"
#include "Platform/io/IO_Main.h"
#include "IPC/IPC_Main.h"
#include "MemoryManagement/DMA_Memory.h"
#include "mmu/Paging_Main.h"
#include "Core/process/ProcessManager.h"
#include "Core/sync/Spinlock.h"
#include "Core/timer/Timer.h"
#include "Drivers/Client/PCI/PCI_Main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DRIVER_FRAMEWORK_MAX_MAPPINGS 128u

typedef enum {
    DRIVER_FRAMEWORK_MAPPING_NONE = 0,
    DRIVER_FRAMEWORK_MAPPING_DMA = 1,
    DRIVER_FRAMEWORK_MAPPING_MMIO = 2,
} driver_framework_mapping_type_t;

typedef struct {
    uint8_t used;
    uint8_t type;
    uint16_t reserved0;
    int32_t owner_pid;
    uint32_t reserved1;
    uint64_t user_base;
    uint64_t user_ptr;
    uint64_t mapped_size;
    uint64_t requested_size;
    uint64_t phys_addr;
    void *kernel_virt;
} driver_framework_mapping_t;

static driver_framework_mapping_t g_mappings[DRIVER_FRAMEWORK_MAX_MAPPINGS];
static spinlock_t g_mapping_lock;
static uint8_t g_driver_framework_initialized = 0;

static uint64_t driver_framework_align_up_u64(uint64_t value, uint64_t align)
{
    if (align == 0u) {
        return 0u;
    }
    return (value + align - 1u) & ~(align - 1u);
}

static void driver_framework_timer_msleep(uint32_t ms)
{
    timer_apic_sleep_ms(ms);
}

static void driver_framework_init_once(void)
{
    if (g_driver_framework_initialized != 0u) {
        return;
    }

    memset(g_mappings, 0, sizeof(g_mappings));
    spinlock_init(&g_mapping_lock);
    g_driver_framework_initialized = 1u;
}

static void driver_framework_prepare_response(
    const driver_framework_ipc_message_t *request,
    driver_framework_ipc_message_t *response)
{
    memset(response, 0, sizeof(*response));
    response->magic = DRIVER_FRAMEWORK_IPC_MAGIC;
    response->version = DRIVER_FRAMEWORK_API_VERSION;
    response->opcode = request->opcode;
    response->request_id = request->request_id;
    response->status = OS_STATUS_NOT_SUPPORTED;
}

static os_status_t driver_framework_send_response(
    int32_t target_pid,
    const driver_framework_ipc_message_t *response)
{
    return ipc_send_message_from_pid(DRIVER_FRAMEWORK_KERNEL_ENDPOINT_PID,
                                     target_pid,
                                     response,
                                     (uint32_t)sizeof(*response));
}

static os_status_t driver_framework_register_mapping(
    const driver_framework_mapping_t *mapping)
{
    uint64_t irq_flags;

    if (mapping == NULL) {
        return OS_STATUS_INVALID_ARG;
    }

    irq_flags = irq_save_disable();
    spinlock_lock(&g_mapping_lock);

    for (uint32_t i = 0; i < DRIVER_FRAMEWORK_MAX_MAPPINGS; ++i) {
        if (g_mappings[i].used == 0u) {
            g_mappings[i] = *mapping;
            g_mappings[i].used = 1u;
            spinlock_unlock(&g_mapping_lock);
            irq_restore(irq_flags);
            return OS_STATUS_OK;
        }
    }

    spinlock_unlock(&g_mapping_lock);
    irq_restore(irq_flags);
    return OS_STATUS_LIMIT_REACHED;
}

static os_status_t driver_framework_take_mapping(int32_t owner_pid,
                                                 uint64_t user_ptr,
                                                 uint8_t type,
                                                 driver_framework_mapping_t *out_mapping)
{
    uint64_t irq_flags;

    if (out_mapping == NULL) {
        return OS_STATUS_INVALID_ARG;
    }

    irq_flags = irq_save_disable();
    spinlock_lock(&g_mapping_lock);

    for (uint32_t i = 0; i < DRIVER_FRAMEWORK_MAX_MAPPINGS; ++i) {
        driver_framework_mapping_t *mapping = &g_mappings[i];
        if (mapping->used == 0u ||
            mapping->owner_pid != owner_pid ||
            mapping->user_ptr != user_ptr) {
            continue;
        }

        if (type != (uint8_t)DRIVER_FRAMEWORK_MAPPING_NONE &&
            mapping->type != type) {
            continue;
        }

        *out_mapping = *mapping;
        memset(mapping, 0, sizeof(*mapping));
        spinlock_unlock(&g_mapping_lock);
        irq_restore(irq_flags);
        return OS_STATUS_OK;
    }

    spinlock_unlock(&g_mapping_lock);
    irq_restore(irq_flags);
    return OS_STATUS_NOT_FOUND;
}

static os_status_t driver_framework_translate_mapping(int32_t owner_pid,
                                                      uint64_t user_ptr,
                                                      uint64_t *phys_out)
{
    uint64_t irq_flags;

    if (phys_out == NULL) {
        return OS_STATUS_INVALID_ARG;
    }

    irq_flags = irq_save_disable();
    spinlock_lock(&g_mapping_lock);

    for (uint32_t i = 0; i < DRIVER_FRAMEWORK_MAX_MAPPINGS; ++i) {
        const driver_framework_mapping_t *mapping = &g_mappings[i];
        uint64_t delta;

        if (mapping->used == 0u || mapping->owner_pid != owner_pid) {
            continue;
        }

        if (user_ptr < mapping->user_ptr) {
            continue;
        }

        delta = user_ptr - mapping->user_ptr;
        if (delta >= mapping->requested_size) {
            continue;
        }

        *phys_out = mapping->phys_addr + delta;
        spinlock_unlock(&g_mapping_lock);
        irq_restore(irq_flags);
        return OS_STATUS_OK;
    }

    spinlock_unlock(&g_mapping_lock);
    irq_restore(irq_flags);
    return OS_STATUS_NOT_FOUND;
}

static os_status_t driver_framework_map_external_range(uint64_t phys_base,
                                                       uint64_t mapped_size,
                                                       uint64_t flags,
                                                       uint64_t *user_base_out)
{
    void *user_base_ptr;
    uint64_t cr3;

    if (mapped_size == 0u || user_base_out == NULL) {
        return OS_STATUS_INVALID_ARG;
    }

    user_base_ptr = process_user_mmap(mapped_size, 0u);
    if (user_base_ptr == NULL) {
        return OS_STATUS_FAULT;
    }

    cr3 = process_get_current_cr3();
    if (cr3 == 0u) {
        (void)process_user_free(user_base_ptr);
        return OS_STATUS_FAULT;
    }

    *user_base_out = (uint64_t)(uintptr_t)user_base_ptr;
    for (uint64_t page_offset = 0u; page_offset < mapped_size; page_offset += PAGE_SIZE) {
        if (paging_map_user_page(cr3,
                                 *user_base_out + page_offset,
                                 phys_base + page_offset,
                                 flags | PAGE_EXTERNAL) < 0) {
            (void)paging_unmap_range(cr3, *user_base_out, mapped_size);
            (void)process_user_free(user_base_ptr);
            return OS_STATUS_FAULT;
        }
    }

    return OS_STATUS_OK;
}

static os_status_t driver_framework_handle_dma_alloc(
    int32_t sender_pid,
    const driver_framework_ipc_message_t *request,
    driver_framework_ipc_message_t *response)
{
    driver_framework_mapping_t mapping;
    void *kernel_ptr;
    uint64_t phys_addr = 0u;
    uint64_t phys_base;
    uint64_t offset;
    uint64_t mapped_size;
    uint64_t user_base = 0u;
    os_status_t status;

    if (request->value0 == 0u || request->value0 > (uint64_t)UINT32_MAX) {
        return OS_STATUS_INVALID_ARG;
    }

    kernel_ptr = dma_alloc((size_t)request->value0, &phys_addr);
    if (kernel_ptr == NULL) {
        return OS_STATUS_FAULT;
    }

    phys_base = phys_addr & PAGE_MASK;
    offset = phys_addr - phys_base;
    if (request->value0 > (UINT64_MAX - offset)) {
        dma_free(kernel_ptr, (size_t)request->value0);
        return OS_STATUS_INVALID_ARG;
    }

    mapped_size = driver_framework_align_up_u64(request->value0 + offset, PAGE_SIZE);
    if (mapped_size == 0u || mapped_size > (uint64_t)UINT32_MAX) {
        dma_free(kernel_ptr, (size_t)request->value0);
        return OS_STATUS_INVALID_ARG;
    }

    status = driver_framework_map_external_range(phys_base,
                                                 mapped_size,
                                                 PAGE_RW,
                                                 &user_base);
    if (status != OS_STATUS_OK) {
        dma_free(kernel_ptr, (size_t)request->value0);
        return status;
    }

    memset(&mapping, 0, sizeof(mapping));
    mapping.used = 1u;
    mapping.type = (uint8_t)DRIVER_FRAMEWORK_MAPPING_DMA;
    mapping.owner_pid = sender_pid;
    mapping.user_base = user_base;
    mapping.user_ptr = user_base + offset;
    mapping.mapped_size = mapped_size;
    mapping.requested_size = request->value0;
    mapping.phys_addr = phys_addr;
    mapping.kernel_virt = kernel_ptr;

    status = driver_framework_register_mapping(&mapping);
    if (status != OS_STATUS_OK) {
        (void)paging_unmap_range(process_get_current_cr3(), user_base, mapped_size);
        (void)process_user_free((void *)(uintptr_t)user_base);
        dma_free(kernel_ptr, (size_t)request->value0);
        return status;
    }

    response->value0 = mapping.user_ptr;
    response->value1 = mapping.phys_addr;
    response->value2 = mapping.mapped_size;
    return OS_STATUS_OK;
}

static os_status_t driver_framework_handle_dma_free(
    int32_t sender_pid,
    const driver_framework_ipc_message_t *request)
{
    driver_framework_mapping_t mapping;
    os_status_t status;

    status = driver_framework_take_mapping(sender_pid,
                                           request->value0,
                                           (uint8_t)DRIVER_FRAMEWORK_MAPPING_DMA,
                                           &mapping);
    if (status != OS_STATUS_OK) {
        return status;
    }

    if (paging_unmap_range(process_get_current_cr3(),
                           mapping.user_base,
                           mapping.mapped_size) < 0) {
        return OS_STATUS_FAULT;
    }

    (void)process_user_free((void *)(uintptr_t)mapping.user_base);
    dma_free(mapping.kernel_virt, (size_t)mapping.requested_size);
    return OS_STATUS_OK;
}

static os_status_t driver_framework_handle_mmio_map(
    int32_t sender_pid,
    const driver_framework_ipc_message_t *request,
    driver_framework_ipc_message_t *response)
{
    driver_framework_mapping_t mapping;
    uint64_t phys_base;
    uint64_t offset;
    uint64_t mapped_size;
    uint64_t user_base = 0u;
    os_status_t status;

    if (request->value1 == 0u || request->value1 > (uint64_t)UINT32_MAX) {
        return OS_STATUS_INVALID_ARG;
    }

    phys_base = request->value0 & PAGE_MASK;
    offset = request->value0 - phys_base;
    if (request->value1 > (UINT64_MAX - offset)) {
        return OS_STATUS_INVALID_ARG;
    }

    mapped_size = driver_framework_align_up_u64(request->value1 + offset, PAGE_SIZE);
    if (mapped_size == 0u || mapped_size > (uint64_t)UINT32_MAX) {
        return OS_STATUS_INVALID_ARG;
    }

    status = driver_framework_map_external_range(phys_base,
                                                 mapped_size,
                                                 PAGE_RW | PAGE_PCD | PAGE_PWT,
                                                 &user_base);
    if (status != OS_STATUS_OK) {
        return status;
    }

    memset(&mapping, 0, sizeof(mapping));
    mapping.used = 1u;
    mapping.type = (uint8_t)DRIVER_FRAMEWORK_MAPPING_MMIO;
    mapping.owner_pid = sender_pid;
    mapping.user_base = user_base;
    mapping.user_ptr = user_base + offset;
    mapping.mapped_size = mapped_size;
    mapping.requested_size = request->value1;
    mapping.phys_addr = request->value0;
    mapping.kernel_virt = NULL;

    status = driver_framework_register_mapping(&mapping);
    if (status != OS_STATUS_OK) {
        (void)paging_unmap_range(process_get_current_cr3(), user_base, mapped_size);
        (void)process_user_free((void *)(uintptr_t)user_base);
        return status;
    }

    response->value0 = mapping.user_ptr;
    response->value1 = mapping.phys_addr;
    response->value2 = mapping.mapped_size;
    return OS_STATUS_OK;
}

static os_status_t driver_framework_handle_mmio_unmap(
    int32_t sender_pid,
    const driver_framework_ipc_message_t *request)
{
    driver_framework_mapping_t mapping;
    os_status_t status;

    status = driver_framework_take_mapping(sender_pid,
                                           request->value0,
                                           (uint8_t)DRIVER_FRAMEWORK_MAPPING_MMIO,
                                           &mapping);
    if (status != OS_STATUS_OK) {
        return status;
    }

    if (paging_unmap_range(process_get_current_cr3(),
                           mapping.user_base,
                           mapping.mapped_size) < 0) {
        return OS_STATUS_FAULT;
    }

    (void)process_user_free((void *)(uintptr_t)mapping.user_base);
    return OS_STATUS_OK;
}

int driver_framework_api_is_endpoint_pid(int32_t pid)
{
    return pid == DRIVER_FRAMEWORK_KERNEL_ENDPOINT_PID ? 1 : 0;
}

os_status_t driver_framework_api_handle_ipc(int32_t sender_pid,
                                            const void *message,
                                            uint32_t size)
{
    const driver_framework_ipc_message_t *request;
    driver_framework_ipc_message_t response;
    os_status_t status;

    driver_framework_init_once();

    if (sender_pid < 0 || message == NULL || size != sizeof(driver_framework_ipc_message_t)) {
        return OS_STATUS_INVALID_ARG;
    }

    request = (const driver_framework_ipc_message_t *)message;
    driver_framework_prepare_response(request, &response);

    if (request->magic != DRIVER_FRAMEWORK_IPC_MAGIC ||
        request->version != DRIVER_FRAMEWORK_API_VERSION ||
        request->data_size > DRIVER_FRAMEWORK_INLINE_DATA_SIZE) {
        response.status = OS_STATUS_INVALID_ARG;
        return driver_framework_send_response(sender_pid, &response);
    }

    switch ((driver_framework_opcode_t)request->opcode) {
        case DRIVER_FRAMEWORK_OP_TIMER_MSLEEP:
            driver_framework_timer_msleep((uint32_t)request->value0);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_TIMER_HZ:
            response.value0 = (uint64_t)timer_hz();
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_TIMER_TICKS:
            response.value0 = timer_ticks();
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_IO_IN8:
            response.value0 = (uint64_t)inb((uint16_t)request->value0);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_IO_OUT8:
            outb((uint16_t)request->value0, (uint8_t)request->value1);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_IO_IN16:
            response.value0 = (uint64_t)inw((uint16_t)request->value0);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_IO_OUT16:
            outw((uint16_t)request->value0, (uint16_t)request->value1);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_IO_IN32:
            response.value0 = (uint64_t)inl((uint16_t)request->value0);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_IO_OUT32:
            outl((uint16_t)request->value0, (uint32_t)request->value1);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_PCI_READ_CONFIG:
            response.value0 = (uint64_t)pci_read_config((uint8_t)request->value0,
                                                        (uint8_t)request->value1,
                                                        (uint8_t)request->value2,
                                                        (uint8_t)request->value3);
            status = OS_STATUS_OK;
            break;

        case DRIVER_FRAMEWORK_OP_PCI_WRITE_CONFIG:
        {
            driver_framework_pci_write_request_t write_request;
            if (request->data_size != sizeof(write_request)) {
                status = OS_STATUS_INVALID_ARG;
                break;
            }
            memcpy(&write_request, request->data, sizeof(write_request));
            pci_write_config((uint8_t)request->value0,
                             (uint8_t)request->value1,
                             (uint8_t)request->value2,
                             write_request.offset,
                             write_request.value);
            status = OS_STATUS_OK;
            break;
        }

        case DRIVER_FRAMEWORK_OP_DMA_ALLOC:
            status = driver_framework_handle_dma_alloc(sender_pid, request, &response);
            break;

        case DRIVER_FRAMEWORK_OP_DMA_FREE:
            status = driver_framework_handle_dma_free(sender_pid, request);
            break;

        case DRIVER_FRAMEWORK_OP_VIRT_TO_PHYS:
            status = driver_framework_translate_mapping(sender_pid,
                                                        request->value0,
                                                        &response.value0);
            break;

        case DRIVER_FRAMEWORK_OP_MMIO_MAP:
            status = driver_framework_handle_mmio_map(sender_pid, request, &response);
            break;

        case DRIVER_FRAMEWORK_OP_MMIO_UNMAP:
            status = driver_framework_handle_mmio_unmap(sender_pid, request);
            break;

        case DRIVER_FRAMEWORK_OP_INVALID:
        default:
            status = OS_STATUS_NOT_SUPPORTED;
            break;
    }

    response.status = status;
    return driver_framework_send_response(sender_pid, &response);
}
