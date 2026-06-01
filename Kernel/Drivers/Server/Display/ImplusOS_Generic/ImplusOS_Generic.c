#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "Platform/io/IO_Main.h"
#include "MemoryManagement/Memory_Main.h"
#include "../Display_Driver.h"
#ifndef IMPLUS_DRIVER_MODULE
#include "mmu/Paging_Main.h"
#include "Drivers/Module/DriverSelect.h"
#endif

#include "Drivers/Module/DriverBinary.h"

#define GENERIC_FB_DEVICE_NAME "Generic Framebuffer"
#define GENERIC_FB_BYTES_PER_PIXEL 4U
#define GENERIC_FB_MAP_GRANULE (2ULL * 1024ULL * 1024ULL)
#define GENERIC_FB_MAX_MAPPINGS 1028

#ifdef IMPLUS_DRIVER_MODULE
static const driver_binary_t *g_driver_api = NULL;

#define malloc g_driver_api->malloc
#define free g_driver_api->free
#define map_mmio_virt g_driver_api->map_mmio_virt
#define memcpy g_driver_api->memcpy
#else
#include <string.h>
#endif

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scan_line;
    uint64_t size_bytes;
    uint64_t map_offset_bytes;
    uint32_t mapped_chunks;
    uint8_t *mapped_bases[GENERIC_FB_MAX_MAPPINGS];
} framebuffer_t;

typedef struct {
    uint32_t *buffer;
    uint64_t buffer_size;
} double_buffer_t;

static framebuffer_t g_fb;
static double_buffer_t g_double_buffer = {
    .buffer = NULL,
    .buffer_size = 0u,
};
static int g_ready = 0;

static bool map_framebuffer_chunks(uint64_t phys_addr, uint64_t size_bytes) {
    if (size_bytes == 0) {
        return false;
    }

    uint64_t base_phys = phys_addr & ~(GENERIC_FB_MAP_GRANULE - 1ULL);
    uint64_t base_offset = phys_addr - base_phys;
    uint64_t total_span = base_offset + size_bytes;
    uint64_t chunks_u64 =
        (total_span + GENERIC_FB_MAP_GRANULE - 1ULL) / GENERIC_FB_MAP_GRANULE;

    if (chunks_u64 == 0 || chunks_u64 > GENERIC_FB_MAX_MAPPINGS) {
        return false;
    }

    for (uint32_t i = 0; i < GENERIC_FB_MAX_MAPPINGS; ++i) {
        g_fb.mapped_bases[i] = NULL;
    }

    for (uint32_t i = 0; i < (uint32_t)chunks_u64; ++i) {
        uint64_t chunk_phys = base_phys + ((uint64_t)i * GENERIC_FB_MAP_GRANULE);
        void *chunk_virt = map_mmio_virt(chunk_phys);
        if (chunk_virt == NULL) {
            return false;
        }
        g_fb.mapped_bases[i] = (uint8_t *)chunk_virt;
    }

    g_fb.map_offset_bytes = base_offset;
    g_fb.mapped_chunks = (uint32_t)chunks_u64;
    return true;
}

static inline volatile uint8_t *resolve_pixel_addr(uint64_t pixel_index) {
    uint64_t byte_offset =
        g_fb.map_offset_bytes + (pixel_index * (uint64_t)GENERIC_FB_BYTES_PER_PIXEL);
    uint64_t chunk_index = byte_offset / GENERIC_FB_MAP_GRANULE;
    if (chunk_index >= g_fb.mapped_chunks) {
        return NULL;
    }

    uint64_t chunk_offset = byte_offset % GENERIC_FB_MAP_GRANULE;
    return (volatile uint8_t *)(g_fb.mapped_bases[chunk_index] + chunk_offset);
}

bool generic_fb_set(const driver_boot_framebuffer_t *framebuffer) {
    g_ready = 0;
    g_fb.mapped_chunks = 0;

    if (g_double_buffer.buffer != NULL) {
        free(g_double_buffer.buffer);
        g_double_buffer.buffer = NULL;
    }

    if (!framebuffer || !framebuffer->addr ||
        framebuffer->width == 0 || framebuffer->height == 0 ||
        framebuffer->pixels_per_scan_line < framebuffer->width) {
        return false;
    }

    uint32_t bytes_per_pixel = framebuffer->bytes_per_pixel;
    if (bytes_per_pixel == 0) {
        bytes_per_pixel = GENERIC_FB_BYTES_PER_PIXEL;
    }
    if (bytes_per_pixel != GENERIC_FB_BYTES_PER_PIXEL) {
        return false;
    }

    uint64_t required_bytes =
        (uint64_t)framebuffer->pixels_per_scan_line *
        (uint64_t)framebuffer->height *
        (uint64_t)bytes_per_pixel;
    if (framebuffer->size_bytes != 0 &&
        required_bytes > framebuffer->size_bytes) {
        return false;
    }

    if ((((uint64_t)(uintptr_t)framebuffer->addr) &
         (GENERIC_FB_BYTES_PER_PIXEL - 1ULL)) != 0ULL) {
        return false;
    }

    if (!map_framebuffer_chunks((uint64_t)(uintptr_t)framebuffer->addr, required_bytes)) {
        return false;
    }

    g_fb.width = framebuffer->width;
    g_fb.height = framebuffer->height;
    g_fb.pixels_per_scan_line = framebuffer->pixels_per_scan_line;
    g_fb.size_bytes = required_bytes;

    uint64_t buffer_size = (uint64_t)g_fb.pixels_per_scan_line * g_fb.height * sizeof(uint32_t);
    uint32_t *buffer = (uint32_t *)malloc(buffer_size);
    if (buffer == NULL) {
        return false;
    }

    for (uint64_t i = 0; i < buffer_size / sizeof(uint32_t); ++i) {
        buffer[i] = 0;
    }

    g_double_buffer.buffer = buffer;
    g_double_buffer.buffer_size = buffer_size;

    g_ready = 1;

    return true;
}

bool fb_probe(void) {
    return g_ready != 0;
}

bool fb_init(void) {
    return g_ready != 0;
}

bool fb_is_ready(void) {
    return g_ready != 0;
}

uint32_t fb_width(void) {
    return g_fb.width;
}

uint32_t fb_height(void) {
    return g_fb.height;
}

void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_ready || !g_double_buffer.buffer) return;
    if (x >= g_fb.width || y >= g_fb.height) return;

    uint64_t pixel_index = (uint64_t)y * g_fb.pixels_per_scan_line + x;
    if (pixel_index < g_double_buffer.buffer_size / sizeof(uint32_t)) {
        g_double_buffer.buffer[pixel_index] = color;
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!g_ready || !g_double_buffer.buffer || w == 0 || h == 0) {
        return;
    }
    if (x >= g_fb.width || y >= g_fb.height) {
        return;
    }

    uint64_t x_end64 = (uint64_t)x + (uint64_t)w;
    uint64_t y_end64 = (uint64_t)y + (uint64_t)h;
    if (x_end64 > g_fb.width) {
        x_end64 = g_fb.width;
    }
    if (y_end64 > g_fb.height) {
        y_end64 = g_fb.height;
    }

    uint32_t x_end = (uint32_t)x_end64;
    uint32_t y_end = (uint32_t)y_end64;
    if (x_end <= x || y_end <= y) {
        return;
    }

    uint64_t buffer_pixels = g_double_buffer.buffer_size / sizeof(uint32_t);
    for (uint32_t py = y; py < y_end; ++py) {
        uint64_t row_start = (uint64_t)py * g_fb.pixels_per_scan_line;
        uint64_t paint_begin = row_start + (uint64_t)x;
        uint64_t paint_end = row_start + (uint64_t)x_end;
        if (paint_begin >= buffer_pixels) {
            break;
        }
        if (paint_end > buffer_pixels) {
            paint_end = buffer_pixels;
        }

        for (uint64_t idx = paint_begin; idx < paint_end; ++idx) {
            g_double_buffer.buffer[idx] = color;
        }
    }
}

void fb_present(void) {
    if (!g_ready || !g_double_buffer.buffer) {
        return;
    }

    uint8_t *src_bytes = (uint8_t *)g_double_buffer.buffer;
    uint64_t row_bytes = (uint64_t)g_fb.width * sizeof(uint32_t);
    uint64_t scan_line_bytes = (uint64_t)g_fb.pixels_per_scan_line * sizeof(uint32_t);

    for (uint32_t y = 0; y < g_fb.height; ++y) {
        uint64_t pixel_index = (uint64_t)y * g_fb.pixels_per_scan_line;
        uint64_t byte_offset = g_fb.map_offset_bytes + (pixel_index * sizeof(uint32_t));
        uint64_t chunk_index = byte_offset / GENERIC_FB_MAP_GRANULE;
        
        if (chunk_index >= g_fb.mapped_chunks) {
            continue;
        }

        uint64_t chunk_offset = byte_offset % GENERIC_FB_MAP_GRANULE;
        uint8_t *dst = g_fb.mapped_bases[chunk_index] + chunk_offset;
        uint8_t *src = src_bytes + (y * scan_line_bytes);
        
        uint64_t remaining_in_chunk = GENERIC_FB_MAP_GRANULE - chunk_offset;

        if (row_bytes <= remaining_in_chunk) {
            memcpy((void *)dst, (const void *)src, row_bytes);
        } else {
            uint64_t part1 = remaining_in_chunk;
            uint64_t part2 = row_bytes - part1;
            memcpy((void *)dst, (const void *)src, part1);
            if (chunk_index + 1 < g_fb.mapped_chunks) {
                memcpy((void *)g_fb.mapped_bases[chunk_index + 1], (const void *)(src + part1), part2);
            }
        }
    }
}

static void *generic_fb_get_framebuffer(void) {
    return g_double_buffer.buffer;
}

static const driver_display_t g_generic_fb_driver = {
    .name = GENERIC_FB_DEVICE_NAME,
    .probe = fb_probe,
    .init = fb_init,
    .is_ready = fb_is_ready,
    .width = fb_width,
    .height = fb_height,
    .draw_pixel = fb_draw_pixel,
    .fill_rect = fb_fill_rect,
    .present = fb_present,
    .set_framebuffer = generic_fb_set,
    .get_framebuffer = generic_fb_get_framebuffer,
};

#ifdef IMPLUS_DRIVER_MODULE

static void generic_fb_driver_shutdown(void)
{
    (void)generic_fb_set(NULL);
    g_driver_api = NULL;
}

static const driver_module_descriptor_t g_generic_fb_module = {
    .magic = DRIVER_DESCRIPTOR_MAGIC,
    .version = DRIVER_DESCRIPTOR_VERSION,
    .kind = DEVICE_TYPE_DISPLAY,
    .load_priority = 90u,
    .deps = { NULL },
    .driver_api = &g_generic_fb_driver,
    .shutdown = generic_fb_driver_shutdown,
};

#undef malloc
#undef free
#undef map_mmio_virt
#undef memcpy

const driver_module_descriptor_t *driver_module_init(const driver_binary_t *api) {
    if (!api || !api->malloc || !api->free ||
        !api->map_mmio_virt || !api->memcpy) {
        return NULL;
    }

    g_driver_api = api;
    return &g_generic_fb_module;
}

#endif
