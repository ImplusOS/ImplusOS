#ifndef IMPLUSOS_BIOS_HANDOFF_H
#define IMPLUSOS_BIOS_HANDOFF_H

#include <stdint.h>

#define BIOS_BOOT_PARAMS_SIGNATURE 0x534f4942u
#define BIOS_BOOT_INFO_ADDRESS     0x00007000u
#define BIOS_MEMORY_MAP_ADDRESS    0x00007200u
#define BIOS_SECTOR_BUFFER_ADDRESS 0x00007c00u
#define BIOS_KERNEL_ELF_BUFFER     0x00100000u

#pragma pack(push, 1)
typedef struct {
    uint32_t signature;
    uint32_t version;
    uint8_t  boot_drive;
    uint8_t  reserved0[7];

    uint64_t partition_start_lba;

    uint64_t framebuffer_base;
    uint32_t framebuffer_size;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixels_per_scan_line;
    uint32_t vbe_mode;

    uint64_t e820_map;
    uint32_t e820_count;
    uint32_t e820_desc_size;

    uint64_t acpi_rsdp;

    uint32_t read_sector_ptr;
    uint32_t enter_kernel_ptr;
} BIOS_BOOT_PARAMS;
#pragma pack(pop)

#endif
