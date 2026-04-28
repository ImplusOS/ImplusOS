#include "DriverFrameworkAPI.h"

#include "../../API/Error.h"
#include "../../API/Process.h"

#include <string.h>

static ipc_message_t g_pending_messages[DRIVER_FRAMEWORK_PENDING_MESSAGE_MAX];
static uint32_t g_pending_message_count = 0;
static uint64_t g_next_request_id = 1;

static int32_t driver_framework_pending_push(const ipc_message_t *message)
{
    if (message == NULL) {
        os_set_errno(22);
        return -1;
    }

    if (g_pending_message_count >= DRIVER_FRAMEWORK_PENDING_MESSAGE_MAX) {
        os_set_errno(24);
        return -1;
    }

    g_pending_messages[g_pending_message_count] = *message;
    g_pending_message_count++;
    os_clear_errno();
    return 0;
}

static int32_t driver_framework_pending_pop(ipc_message_t *out_message)
{
    if (out_message == NULL) {
        os_set_errno(22);
        return -1;
    }

    if (g_pending_message_count == 0u) {
        os_set_errno(2);
        return -1;
    }

    *out_message = g_pending_messages[0];
    if (g_pending_message_count > 1u) {
        memmove(&g_pending_messages[0],
                &g_pending_messages[1],
                (size_t)(g_pending_message_count - 1u) * sizeof(ipc_message_t));
    }
    g_pending_message_count--;
    os_clear_errno();
    return 0;
}

static int32_t driver_framework_pending_take_reply(uint64_t request_id,
                                                   driver_framework_ipc_message_t *out_reply)
{
    if (out_reply == NULL) {
        os_set_errno(22);
        return -1;
    }

    for (uint32_t i = 0; i < g_pending_message_count; ++i) {
        const ipc_message_t *message = &g_pending_messages[i];
        if (message->sender_pid != DRIVER_FRAMEWORK_KERNEL_ENDPOINT_PID ||
            message->size != sizeof(driver_framework_ipc_message_t)) {
            continue;
        }

        const driver_framework_ipc_message_t *candidate =
            (const driver_framework_ipc_message_t *)message->data;
        if (candidate->magic != DRIVER_FRAMEWORK_IPC_MAGIC ||
            candidate->version != DRIVER_FRAMEWORK_API_VERSION ||
            candidate->request_id != request_id) {
            continue;
        }

        *out_reply = *candidate;
        if (i + 1u < g_pending_message_count) {
            memmove(&g_pending_messages[i],
                    &g_pending_messages[i + 1u],
                    (size_t)(g_pending_message_count - i - 1u) * sizeof(ipc_message_t));
        }
        g_pending_message_count--;
        os_clear_errno();
        return 0;
    }

    os_set_errno(2);
    return -1;
}

static int32_t driver_framework_finalize_call(driver_framework_ipc_message_t *message)
{
    if (message->status < 0) {
        os_set_errno(os_status_to_errno(message->status));
        return -1;
    }

    os_clear_errno();
    return 0;
}

int32_t driver_framework_receive_message(ipc_message_t *out_message)
{
    if (out_message == NULL) {
        os_set_errno(22);
        return -1;
    }

    if (driver_framework_pending_pop(out_message) == 0) {
        return 0;
    }

    return ipc_receive_message(out_message);
}

int32_t driver_framework_call(driver_framework_ipc_message_t *message)
{
    if (message == NULL) {
        os_set_errno(22);
        return -1;
    }

    if (message->data_size > DRIVER_FRAMEWORK_INLINE_DATA_SIZE) {
        os_set_errno(22);
        return -1;
    }

    message->magic = DRIVER_FRAMEWORK_IPC_MAGIC;
    message->version = DRIVER_FRAMEWORK_API_VERSION;
    message->reserved0 = 0u;
    message->request_id = g_next_request_id++;
    message->status = 0;

    if (ipc_send_message(DRIVER_FRAMEWORK_KERNEL_ENDPOINT_PID,
                         message,
                         sizeof(driver_framework_ipc_message_t)) < 0) {
        return -1;
    }

    if (driver_framework_pending_take_reply(message->request_id, message) == 0) {
        return driver_framework_finalize_call(message);
    }

    while (1) {
        ipc_message_t incoming;
        if (ipc_receive_message(&incoming) == 0) {
            if (incoming.sender_pid == DRIVER_FRAMEWORK_KERNEL_ENDPOINT_PID &&
                incoming.size == sizeof(driver_framework_ipc_message_t)) {
                const driver_framework_ipc_message_t *reply =
                    (const driver_framework_ipc_message_t *)incoming.data;
                if (reply->magic == DRIVER_FRAMEWORK_IPC_MAGIC &&
                    reply->version == DRIVER_FRAMEWORK_API_VERSION &&
                    reply->request_id == message->request_id) {
                    *message = *reply;
                    return driver_framework_finalize_call(message);
                }
            }

            if (driver_framework_pending_push(&incoming) < 0) {
                return -1;
            }
        }
        process_yield();
    }
}

int32_t driver_framework_timer_msleep(uint32_t ms)
{
    driver_framework_ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_TIMER_MSLEEP;
    message.value0 = (uint64_t)ms;
    return driver_framework_call(&message);
}

int32_t driver_framework_timer_hz(uint32_t *out_hz)
{
    driver_framework_ipc_message_t message;
    if (out_hz == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_TIMER_HZ;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_hz = (uint32_t)message.value0;
    return 0;
}

int32_t driver_framework_timer_ticks(uint64_t *out_ticks)
{
    driver_framework_ipc_message_t message;
    if (out_ticks == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_TIMER_TICKS;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_ticks = message.value0;
    return 0;
}

int32_t driver_framework_in8(uint16_t port, uint8_t *out_value)
{
    driver_framework_ipc_message_t message;
    if (out_value == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_IO_IN8;
    message.value0 = (uint64_t)port;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_value = (uint8_t)message.value0;
    return 0;
}

int32_t driver_framework_out8(uint16_t port, uint8_t value)
{
    driver_framework_ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_IO_OUT8;
    message.value0 = (uint64_t)port;
    message.value1 = (uint64_t)value;
    return driver_framework_call(&message);
}

int32_t driver_framework_in16(uint16_t port, uint16_t *out_value)
{
    driver_framework_ipc_message_t message;
    if (out_value == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_IO_IN16;
    message.value0 = (uint64_t)port;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_value = (uint16_t)message.value0;
    return 0;
}

int32_t driver_framework_out16(uint16_t port, uint16_t value)
{
    driver_framework_ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_IO_OUT16;
    message.value0 = (uint64_t)port;
    message.value1 = (uint64_t)value;
    return driver_framework_call(&message);
}

int32_t driver_framework_in32(uint16_t port, uint32_t *out_value)
{
    driver_framework_ipc_message_t message;
    if (out_value == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_IO_IN32;
    message.value0 = (uint64_t)port;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_value = (uint32_t)message.value0;
    return 0;
}

int32_t driver_framework_out32(uint16_t port, uint32_t value)
{
    driver_framework_ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_IO_OUT32;
    message.value0 = (uint64_t)port;
    message.value1 = (uint64_t)value;
    return driver_framework_call(&message);
}

int32_t driver_framework_pci_read_config(uint8_t bus,
                                         uint8_t device,
                                         uint8_t func,
                                         uint8_t offset,
                                         uint32_t *out_value)
{
    driver_framework_ipc_message_t message;
    if (out_value == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_PCI_READ_CONFIG;
    message.value0 = (uint64_t)bus;
    message.value1 = (uint64_t)device;
    message.value2 = (uint64_t)func;
    message.value3 = (uint64_t)offset;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_value = (uint32_t)message.value0;
    return 0;
}

int32_t driver_framework_pci_write_config(uint8_t bus,
                                          uint8_t device,
                                          uint8_t func,
                                          uint8_t offset,
                                          uint32_t value)
{
    driver_framework_ipc_message_t message;
    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_PCI_WRITE_CONFIG;
    message.value0 = (uint64_t)bus;
    message.value1 = (uint64_t)device;
    message.value2 = (uint64_t)func;
    message.value3 = (((uint64_t)offset & 0xFFu) << 32) | (uint64_t)value;
    return driver_framework_call(&message);
}

int32_t driver_framework_dma_alloc(size_t size,
                                   void **out_user_ptr,
                                   uint64_t *out_phys_addr)
{
    driver_framework_ipc_message_t message;
    if (size == 0u || out_user_ptr == NULL || out_phys_addr == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_DMA_ALLOC;
    message.value0 = (uint64_t)size;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_user_ptr = (void *)(uintptr_t)message.value0;
    *out_phys_addr = message.value1;
    return 0;
}

int32_t driver_framework_dma_free(void *user_ptr)
{
    driver_framework_ipc_message_t message;
    if (user_ptr == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_DMA_FREE;
    message.value0 = (uint64_t)(uintptr_t)user_ptr;
    return driver_framework_call(&message);
}

int32_t driver_framework_virt_to_phys(const void *ptr, uint64_t *out_phys_addr)
{
    driver_framework_ipc_message_t message;
    if (ptr == NULL || out_phys_addr == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_VIRT_TO_PHYS;
    message.value0 = (uint64_t)(uintptr_t)ptr;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_phys_addr = message.value0;
    return 0;
}

int32_t driver_framework_map_mmio(uint64_t phys_addr,
                                  size_t size,
                                  volatile void **out_ptr)
{
    driver_framework_ipc_message_t message;
    if (size == 0u || out_ptr == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_MMIO_MAP;
    message.value0 = phys_addr;
    message.value1 = (uint64_t)size;
    if (driver_framework_call(&message) < 0) {
        return -1;
    }

    *out_ptr = (volatile void *)(uintptr_t)message.value0;
    return 0;
}

int32_t driver_framework_unmap_mmio(volatile void *ptr)
{
    driver_framework_ipc_message_t message;
    if (ptr == NULL) {
        os_set_errno(22);
        return -1;
    }

    memset(&message, 0, sizeof(message));
    message.opcode = DRIVER_FRAMEWORK_OP_MMIO_UNMAP;
    message.value0 = (uint64_t)(uintptr_t)ptr;
    return driver_framework_call(&message);
}
