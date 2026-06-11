#ifndef MEMORY_MAIN_H
#define MEMORY_MAIN_H

#include <stdint.h>
#include <stddef.h>

void* malloc(uint64_t size);
void* malloc_sensitive(uint64_t size);
void  free(void* ptr);
void  free_sensitive(void* ptr);
void* calloc(uint64_t num, uint64_t size);
void* realloc(void* ptr, uint64_t new_size);



void memory_init(void);
void init_physical_memory(void *memory_map, size_t map_size, size_t desc_size, uint64_t detected_pages);
void physical_memory_reserve_region(uint64_t base, uint64_t size);

void* alloc_page(void);
void  free_page(void* addr);
void* alloc_contiguous_pages(uint32_t page_count, uint32_t align_pages);
void  free_contiguous_pages(void* addr, uint32_t page_count);

uint64_t get_free_memory(void);
uint64_t get_used_memory(void);
uint64_t get_total_memory_pages(void);
void     memory_dump_virtual(const void *addr, uint32_t bytes);
void     memory_dump_physical(uint64_t phys_addr, uint32_t bytes);

#endif
