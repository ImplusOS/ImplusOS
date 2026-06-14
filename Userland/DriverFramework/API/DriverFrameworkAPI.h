#pragma once

#include <stddef.h>
#include <stdint.h>

#define DRIVER_FRAMEWORK_IPC_MAX_SIZE 256u

#ifndef KERNEL
#include "../../API/IPC.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DRIVER_FRAMEWORK_API_VERSION 1u
#define DRIVER_FRAMEWORK_IPC_MAGIC 0x44524150u

#define DRIVER_FRAMEWORK_KERNEL_ENDPOINT_PID (-4096)
#define DRIVER_FRAMEWORK_INLINE_DATA_SIZE 192u
#define DRIVER_FRAMEWORK_PENDING_MESSAGE_MAX 8u
#define DRIVER_FRAMEWORK_CALL_TIMEOUT_MS 5000u

typedef enum {
    DRIVER_FRAMEWORK_OP_INVALID = 0,
    DRIVER_FRAMEWORK_OP_TIMER_MSLEEP = 1,
    DRIVER_FRAMEWORK_OP_TIMER_HZ = 2,
    DRIVER_FRAMEWORK_OP_TIMER_TICKS = 3,

    DRIVER_FRAMEWORK_OP_IO_IN8 = 10,
    DRIVER_FRAMEWORK_OP_IO_OUT8 = 11,
    DRIVER_FRAMEWORK_OP_IO_IN16 = 12,
    DRIVER_FRAMEWORK_OP_IO_OUT16 = 13,
    DRIVER_FRAMEWORK_OP_IO_IN32 = 14,
    DRIVER_FRAMEWORK_OP_IO_OUT32 = 15,

    DRIVER_FRAMEWORK_OP_PCI_READ_CONFIG = 20,
    DRIVER_FRAMEWORK_OP_PCI_WRITE_CONFIG = 21,

    DRIVER_FRAMEWORK_OP_DMA_ALLOC = 30,
    DRIVER_FRAMEWORK_OP_DMA_FREE = 31,
    DRIVER_FRAMEWORK_OP_VIRT_TO_PHYS = 32,

    DRIVER_FRAMEWORK_OP_MMIO_MAP = 40,
    DRIVER_FRAMEWORK_OP_MMIO_UNMAP = 41,
} driver_framework_opcode_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t opcode;
    uint32_t data_size;
    uint32_t reserved0;
    uint64_t request_id;
    int64_t status;
    uint64_t value0;
    uint64_t value1;
    uint64_t value2;
    uint64_t value3;
    uint8_t data[DRIVER_FRAMEWORK_INLINE_DATA_SIZE];
} driver_framework_ipc_message_t;

typedef struct {
    uint8_t offset;
    uint8_t reserved[3];
    uint32_t value;
} driver_framework_pci_write_request_t;

typedef char driver_framework_ipc_message_size_must_be_256[
    sizeof(driver_framework_ipc_message_t) == DRIVER_FRAMEWORK_IPC_MAX_SIZE ? 1 : -1
];

#ifndef KERNEL
int32_t driver_framework_call(driver_framework_ipc_message_t *message);
int32_t driver_framework_receive_message(ipc_message_t *out_message);

int32_t driver_framework_timer_msleep(uint32_t ms);
int32_t driver_framework_timer_hz(uint32_t *out_hz);
int32_t driver_framework_timer_ticks(uint64_t *out_ticks);

int32_t driver_framework_in8(uint16_t port, uint8_t *out_value);
int32_t driver_framework_out8(uint16_t port, uint8_t value);
int32_t driver_framework_in16(uint16_t port, uint16_t *out_value);
int32_t driver_framework_out16(uint16_t port, uint16_t value);
int32_t driver_framework_in32(uint16_t port, uint32_t *out_value);
int32_t driver_framework_out32(uint16_t port, uint32_t value);

int32_t driver_framework_pci_read_config(uint8_t bus,
                                         uint8_t device,
                                         uint8_t func,
                                         uint8_t offset,
                                         uint32_t *out_value);
int32_t driver_framework_pci_write_config(uint8_t bus,
                                          uint8_t device,
                                          uint8_t func,
                                          uint8_t offset,
                                          uint32_t value);

int32_t driver_framework_dma_alloc(size_t size,
                                   void **out_user_ptr,
                                   uint64_t *out_phys_addr);
int32_t driver_framework_dma_free(void *user_ptr);
int32_t driver_framework_virt_to_phys(const void *ptr, uint64_t *out_phys_addr);

int32_t driver_framework_map_mmio(uint64_t phys_addr,
                                  size_t size,
                                  volatile void **out_ptr);
int32_t driver_framework_unmap_mmio(volatile void *ptr);
#endif

#ifdef __cplusplus
}
#endif
