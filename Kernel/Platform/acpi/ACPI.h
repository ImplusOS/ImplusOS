#pragma once

#include <stdint.h>
#include <stddef.h>

#include "kernel/boot_info.h"

#define ACPI_MAX_CPUS 32

typedef struct {
    uint64_t lapic_base;
    uint64_t ioapic_base;
    uint32_t ioapic_gsi_base;
    uint8_t  cpu_count;
    uint8_t  cpu_apic_ids[ACPI_MAX_CPUS];
    uint32_t pit_gsi;
    uint8_t  pit_level_trigger;
    uint8_t  pit_active_low;
} acpi_info_t;

int  acpi_init(const BOOT_INFO *boot_info);
const acpi_info_t *acpi_get_info(void);
