#ifndef IMPLUSOS_BOOT_PAL_H
#define IMPLUSOS_BOOT_PAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint32_t type;
    uint32_t attribute;
} pal_memory_descriptor_t;

typedef struct {
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    void *(*alloc_pages)(size_t pages, uint64_t *phys_out);
    void (*free_pages)(void *ptr, size_t pages);

    int (*file_open)(const char *path, void **file_handle);
    int (*file_read)(void *file_handle, uint64_t offset, size_t size, void *buffer, size_t *bytes_read);
    void (*file_close)(void *file_handle);
    int (*file_get_size)(void *file_handle, uint64_t *size_out);
    int (*disk_read_sectors)(uint64_t lba, uint32_t count, void *buffer);

    int (*graphics_get_framebuffer)(uint64_t *fb_base, uint64_t *fb_size,
                                    uint32_t *width, uint32_t *height, uint32_t *pitch);
    void (*graphics_present)(void);

    uint64_t (*get_acpi_rsdp)(void);
    int (*get_smbios_info)(char *cpu_name, size_t cpu_max,
                           char *manufacturer, size_t man_max,
                           char *product_name, size_t prod_max);
    int (*get_memory_map)(pal_memory_descriptor_t *map_buffer, size_t *map_size, uint64_t *map_key);

    void (*enter_kernel)(uint64_t entry_point, void *boot_info);
} boot_pal_t;

#endif
