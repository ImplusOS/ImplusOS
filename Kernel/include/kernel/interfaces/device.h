#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum device_type {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_PCI = 1,
    DEVICE_TYPE_DISPLAY = 2,
    DEVICE_TYPE_INPUT = 3,
    DEVICE_TYPE_USB = 4,
    DEVICE_TYPE_NIC = 5,
    DEVICE_TYPE_BLOCK = 6,
    DEVICE_TYPE_AUDIO = 7,
    DEVICE_TYPE_FILESYSTEM = 8,
} device_type_t;

typedef struct device {
    device_type_t type;
    const void *ops;
    void *priv;
    const char *name;
} device_t;
