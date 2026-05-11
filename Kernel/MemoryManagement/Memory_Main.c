#include "Memory_Main.h"
#include "Core/process/ProcessManager.h"
#include "Core/sync/Spinlock.h"
#include "Debug/serial/Serial.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PAGE_SIZE 4096
#define MAX_PAGES_HARD_LIMIT 8388608ULL
#define MAX_ALLOC_PAGE_RECURSION_DEPTH 5

static uint8_t  *g_page_bitmap     = NULL;
static uint64_t  g_max_pages       = 0;
static uint32_t  g_alloc_page_recursion_depth_bsp = 0;
int paging_swap_reclaim_one_page(void);



#define PAGE_BITMAP_STATIC_SIZE 1048576U
#define PAGE_BITMAP_CAPACITY_PAGES ((uint64_t)PAGE_BITMAP_STATIC_SIZE * 8ULL)
static uint8_t g_page_bitmap_static[PAGE_BITMAP_STATIC_SIZE];

enum {
    EFI_LOADER_CODE        = 1,
    EFI_LOADER_DATA        = 2,
    EFI_BOOT_SERVICES_CODE = 3,
    EFI_BOOT_SERVICES_DATA = 4,
    EFI_CONVENTIONAL_MEMORY = 7
};

#define HEAP_MAGIC      0x1BADB002
#define HEAP_MAGIC_FREE 0x2BADB002

typedef struct memory_block {
    uint32_t magic;
    uint64_t size;
    uint8_t  is_free;
    uint8_t  is_sensitive;
    uint16_t reserved;
    struct memory_block *next;
} memory_block_t;

static memory_block_t *heap_start       = NULL;
static memory_block_t *heap_search_hint = NULL;
static uint32_t        heap_initialized = 0;
static uint64_t        used_memory      = 0;

#define HEAP_PAGE_COUNT_MIN  4096U
#define HEAP_PAGE_COUNT_MAX  32768U

static uint32_t heap_page_count = HEAP_PAGE_COUNT_MIN;

static spinlock_t heap_lock;
static spinlock_t page_lock;
static uint32_t   page_alloc_hint = 0;
static uint32_t   g_oom_total     = 0;
static uint32_t   g_oom_malloc    = 0;
static uint32_t   g_oom_pages     = 0;

#define MIN_ALLOC_ALIGN    16u 
#define MIN_SPLIT_REMAINDER 64u

static inline uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1ull) & ~(align - 1ull);
}

static inline uint8_t page_bitmap_get(uint64_t page_index)
{
    uint64_t byte_index = page_index >> 3;
    uint8_t bit_mask = (uint8_t)(1u << (page_index & 7u));
    return (uint8_t)((g_page_bitmap[byte_index] & bit_mask) != 0u);
}

static inline void page_bitmap_set(uint64_t page_index, uint8_t used)
{
    uint64_t byte_index = page_index >> 3;
    uint8_t bit_mask = (uint8_t)(1u << (page_index & 7u));
    if (used != 0u) {
        g_page_bitmap[byte_index] |= bit_mask;
    } else {
        g_page_bitmap[byte_index] &= (uint8_t)~bit_mask;
    }
}

static void memory_report_oom(const char *site, uint64_t request)
{
    (void)site;
    (void)request;
    ++g_oom_total;
}

static memory_block_t *split_block_if_needed(memory_block_t *block, uint64_t size)
{
    if (block->size >= size + sizeof(memory_block_t) + MIN_SPLIT_REMAINDER) {
        memory_block_t *new_block =
            (memory_block_t *)((uint8_t *)block + sizeof(memory_block_t) + size);
        new_block->magic     = HEAP_MAGIC_FREE;
        new_block->size      = block->size - size - sizeof(memory_block_t);
        new_block->is_free   = 1;
        new_block->is_sensitive = 0;
        new_block->next      = block->next;

        block->size = size;
        block->next = new_block;
    }
    return block;
}

static void *malloc_locked(uint64_t size)
{
    if (heap_search_hint == NULL) {
        heap_search_hint = heap_start;
    }

    memory_block_t *start   = heap_search_hint;
    memory_block_t *current = start;
    int wrapped = 0;

    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            current = split_block_if_needed(current, size);
            current->is_free      = 0;
            current->is_sensitive = 0;
            current->magic        = HEAP_MAGIC;
            used_memory += current->size;
            heap_search_hint = (current->next != NULL) ? current->next : heap_start;
            return (void *)((uint8_t *)current + sizeof(memory_block_t));
        }

        current = current->next;
        if (current == NULL && !wrapped) {
            current = heap_start;
            wrapped = 1;
        }
        if (wrapped && current == start) {
            break;
        }
    }
    return NULL;
}

static void mark_pages(uint64_t start_page, uint64_t page_count, uint8_t used)
{
    if (g_page_bitmap == NULL || start_page >= g_max_pages) return;
    uint64_t end_page = start_page + page_count;
    if (end_page > g_max_pages) end_page = g_max_pages;
    for (uint64_t i = start_page; i < end_page; i++) {
        page_bitmap_set(i, used);
    }
}

static int is_usable_memory_type(uint32_t type)
{
    return (type == EFI_BOOT_SERVICES_CODE) ||
           (type == EFI_BOOT_SERVICES_DATA) ||
           (type == EFI_CONVENTIONAL_MEMORY);
}

void init_physical_memory(void *memory_map, size_t map_size, size_t desc_size,
                          uint64_t detected_pages)
{
    uint64_t max_pages = detected_pages;

    if ((max_pages == 0) && memory_map != NULL && desc_size != 0) {
        uint8_t *map = (uint8_t *)memory_map;
        for (size_t offset = 0; offset + desc_size <= map_size; offset += desc_size) {
            typedef struct {
                uint32_t Type; uint32_t Pad;
                uint64_t PhysicalStart;
                uint64_t VirtualStart;
                uint64_t NumberOfPages;
                uint64_t Attribute;
            } EFI_MEM_DESC;
            EFI_MEM_DESC *desc = (EFI_MEM_DESC *)(map + offset);
            uint64_t end_page = (desc->PhysicalStart / PAGE_SIZE) + desc->NumberOfPages;
            if (end_page > max_pages) max_pages = end_page;
        }
    }

    if (max_pages == 0 || max_pages > MAX_PAGES_HARD_LIMIT) {
        max_pages = MAX_PAGES_HARD_LIMIT;
    }
    if (max_pages > PAGE_BITMAP_CAPACITY_PAGES) {
        max_pages = PAGE_BITMAP_CAPACITY_PAGES;
    }

    g_max_pages   = max_pages;
    g_page_bitmap = g_page_bitmap_static;

    memset(g_page_bitmap, 0xFF, PAGE_BITMAP_STATIC_SIZE);

    if (memory_map != NULL && desc_size != 0) {
        uint8_t *map = (uint8_t *)memory_map;
        for (size_t offset = 0; offset + desc_size <= map_size; offset += desc_size) {
            typedef struct {
                uint32_t Type; uint32_t Pad;
                uint64_t PhysicalStart;
                uint64_t VirtualStart;
                uint64_t NumberOfPages;
                uint64_t Attribute;
            } EFI_MEM_DESC;
            EFI_MEM_DESC *desc = (EFI_MEM_DESC *)(map + offset);
            if (is_usable_memory_type(desc->Type)) {
                uint64_t start_page = desc->PhysicalStart / PAGE_SIZE;
                mark_pages(start_page, desc->NumberOfPages, 0);
            }
        }
    }


    uint64_t free_pages = 0;
    for (uint64_t i = 0; i < g_max_pages; i++) {
        if (page_bitmap_get(i) == 0u) free_pages++;
    }
    
    if (g_max_pages > 0) {
        page_bitmap_set(0, 1u);
    }

    heap_page_count = HEAP_PAGE_COUNT_MIN;
    if (free_pages / 4 > (uint64_t)HEAP_PAGE_COUNT_MIN) {
        uint64_t candidate = free_pages / 4;
        if (candidate > (uint64_t)HEAP_PAGE_COUNT_MAX) candidate = (uint64_t)HEAP_PAGE_COUNT_MAX;
        heap_page_count = (uint32_t)candidate;
    }
}

void memory_init(void)
{
    if (heap_initialized) {
        return;
    }

    spinlock_init(&heap_lock);
    spinlock_init(&page_lock);

    heap_start = (memory_block_t *)alloc_contiguous_pages(heap_page_count, 1);
    if (heap_start == NULL) {
        if (heap_page_count > 16384U) {
            heap_page_count = 16384U;
            heap_start = (memory_block_t *)alloc_contiguous_pages(heap_page_count, 1);
        }
        if (heap_start == NULL && heap_page_count > 8192U) {
            heap_page_count = 8192U;
            heap_start = (memory_block_t *)alloc_contiguous_pages(heap_page_count, 1);
        }
        if (heap_start == NULL) {
            heap_page_count = HEAP_PAGE_COUNT_MIN;
            heap_start = (memory_block_t *)alloc_contiguous_pages(heap_page_count, 1);
            if (heap_start == NULL) {
                return;
            }
        }
    }
 
     heap_start->magic       = HEAP_MAGIC_FREE;
    heap_start->size        = ((uint64_t)heap_page_count * PAGE_SIZE) - sizeof(memory_block_t);
    heap_start->is_free     = 1;
    heap_start->is_sensitive = 0;
    heap_start->next        = NULL;
    heap_search_hint        = heap_start;

    heap_initialized  = 1;
    used_memory       = 0;
    page_alloc_hint   = 0;
    g_oom_total       = 0;
    g_oom_malloc      = 0;
    g_oom_pages       = 0;
    g_alloc_page_recursion_depth_bsp = 0;
}

void *malloc(uint64_t size)
{
    if (!heap_initialized) return NULL;
    if (size == 0) return NULL;
    size = align_up(size, MIN_ALLOC_ALIGN);
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&heap_lock);
    void *ptr = malloc_locked(size);
    spinlock_unlock(&heap_lock);
    irq_restore(irq_flags);
    if (ptr != NULL) return ptr;
    ++g_oom_malloc;
    memory_report_oom("malloc", size);
    return NULL;
}

void free(void *ptr)
{
    if (ptr == NULL) return;
    if (!heap_initialized || heap_start == NULL) return;
    uintptr_t addr           = (uintptr_t)ptr;
    uintptr_t heap_start_addr = (uintptr_t)heap_start;
    uintptr_t heap_end_addr   = heap_start_addr + ((uint64_t)heap_page_count * PAGE_SIZE);
    if (addr < heap_start_addr + sizeof(memory_block_t) || addr >= heap_end_addr) return;
    memory_block_t *block = (memory_block_t *)(addr - sizeof(memory_block_t));
    if (block->magic != HEAP_MAGIC) return;
    if (block->is_free) return;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&heap_lock);
    block->is_free = 1;
    block->magic   = HEAP_MAGIC_FREE;
    used_memory   -= block->size;
    if (block->is_sensitive) {
        uint8_t *payload = (uint8_t *)ptr;
        for (uint64_t i = 0; i < block->size; i++) payload[i] = 0;
        block->is_sensitive = 0;
    }
    if (block->next != NULL && block->next->is_free) {
        block->size += sizeof(memory_block_t) + block->next->size;
        block->next  = block->next->next;
    }
    if (addr > heap_start_addr + sizeof(memory_block_t)) {
        memory_block_t *prev = heap_start;
        while (prev != NULL && prev->next != block) prev = prev->next;
        if (prev != NULL && prev->is_free) {
            prev->size += sizeof(memory_block_t) + block->size;
            prev->next  = block->next;
            block = prev;
        }
    }
    heap_search_hint = block;
    spinlock_unlock(&heap_lock);
    irq_restore(irq_flags);
}

void *malloc_sensitive(uint64_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) return NULL;
    memory_block_t *block = (memory_block_t *)((uintptr_t)ptr - sizeof(memory_block_t));
    block->is_sensitive = 1;
    return ptr;
}

void free_sensitive(void *ptr) {
    if (ptr == NULL) return;
    memory_block_t *block = (memory_block_t *)((uintptr_t)ptr - sizeof(memory_block_t));
    if (block->magic == HEAP_MAGIC) block->is_sensitive = 1;
    free(ptr);
}

void *calloc(uint64_t num, uint64_t size) {
    if (num != 0 && size > UINT64_MAX / num) return NULL;
    uint64_t total_size = num * size;
    void *ptr = malloc(total_size);
    if (ptr != NULL) {
        uint8_t *byte_ptr = (uint8_t *)ptr;
        for (uint64_t i = 0; i < total_size; i++) byte_ptr[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, uint64_t new_size) {
    if (ptr == NULL) return malloc(new_size);
    if (new_size == 0) { free(ptr); return NULL; }
    new_size = align_up(new_size, MIN_ALLOC_ALIGN);
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&heap_lock);
    memory_block_t *block = (memory_block_t *)((uintptr_t)ptr - sizeof(memory_block_t));
    if (block->magic != HEAP_MAGIC || block->is_free) {
        spinlock_unlock(&heap_lock);
        irq_restore(irq_flags);
        return NULL;
    }
    uint64_t old_size    = block->size;
    uint8_t  was_sensitive = block->is_sensitive;
    if (new_size <= old_size) {
        split_block_if_needed(block, new_size);
        spinlock_unlock(&heap_lock);
        irq_restore(irq_flags);
        return ptr;
    }
    if (block->next != NULL && block->next->is_free && block->size + sizeof(memory_block_t) + block->next->size >= new_size) {
        uint64_t added = sizeof(memory_block_t) + block->next->size;
        block->size += added;
        block->next  = block->next->next;
        used_memory += added;
        split_block_if_needed(block, new_size);
        spinlock_unlock(&heap_lock);
        irq_restore(irq_flags);
        return ptr;
    }
    spinlock_unlock(&heap_lock);
    irq_restore(irq_flags);
    void *new_ptr = malloc(new_size);
    if (new_ptr == NULL) return NULL;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (uint64_t i = 0; i < old_size; i++) dst[i] = src[i];
    if (was_sensitive) {
        memory_block_t *nb = (memory_block_t *)((uintptr_t)new_ptr - sizeof(memory_block_t));
        nb->is_sensitive = 1;
    }
    free(ptr);
    return new_ptr;
}

uint64_t get_total_memory_pages(void) { return g_max_pages; }

uint64_t get_free_memory(void) {
    if (!heap_initialized) return 0;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&heap_lock);
    uint64_t free_memory = 0;
    memory_block_t *current = heap_start;
    while (current != NULL) {
        if (current->is_free) free_memory += current->size;
        current = current->next;
    }
    spinlock_unlock(&heap_lock);
    irq_restore(irq_flags);
    return free_memory;
}

uint64_t get_used_memory(void) {
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&heap_lock);
    uint64_t used = used_memory;
    spinlock_unlock(&heap_lock);
    irq_restore(irq_flags);
    return used;
}

void *alloc_page(void) {
    if (g_page_bitmap == NULL) return NULL;
    if (g_alloc_page_recursion_depth_bsp >= MAX_ALLOC_PAGE_RECURSION_DEPTH) {
        ++g_oom_pages;
        memory_report_oom("alloc_page (recursion limit)", PAGE_SIZE);
        return NULL;
    }
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&page_lock);
    for (uint64_t offset = 0; offset < g_max_pages; offset++) {
        uint64_t i = ((uint64_t)page_alloc_hint + offset) % g_max_pages;
        if (page_bitmap_get(i) == 0u) {
            page_bitmap_set(i, 1u);
            page_alloc_hint  = (uint32_t)((i + 1) % g_max_pages);
            spinlock_unlock(&page_lock);
            irq_restore(irq_flags);
            return (void *)(i * PAGE_SIZE);
        }
    }
    spinlock_unlock(&page_lock);
    irq_restore(irq_flags);
    ++g_alloc_page_recursion_depth_bsp;
    int reclaim_result = paging_swap_reclaim_one_page();
    --g_alloc_page_recursion_depth_bsp;
    if (reclaim_result > 0) return alloc_page();
    ++g_oom_pages;
    memory_report_oom("alloc_page", PAGE_SIZE);
    return NULL;
}

void *alloc_contiguous_pages(uint32_t page_count, uint32_t align_pages) {
    if (g_page_bitmap == NULL || page_count == 0) return NULL;
    if ((uint64_t)page_count > g_max_pages) return NULL;
    if (g_alloc_page_recursion_depth_bsp >= MAX_ALLOC_PAGE_RECURSION_DEPTH) {
        ++g_oom_pages;
        memory_report_oom("alloc_contiguous_pages (recursion limit)", (uint64_t)page_count * PAGE_SIZE);
        return NULL;
    }
    if (align_pages == 0) align_pages = 1;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&page_lock);
    uint64_t limit = g_max_pages - (uint64_t)page_count;
    for (uint64_t start = 0; start <= limit; ++start) {
        if ((start % align_pages) != 0) continue;
        uint32_t run = 0;
        while (run < page_count && page_bitmap_get(start + run) == 0u) ++run;
        if (run != page_count) { start += run; continue; }
        for (uint32_t i = 0; i < page_count; ++i) page_bitmap_set(start + i, 1u);
        page_alloc_hint = (uint32_t)((start + page_count) % g_max_pages);
        spinlock_unlock(&page_lock);
        irq_restore(irq_flags);
        return (void *)((uintptr_t)start * PAGE_SIZE);
    }
    spinlock_unlock(&page_lock);
    irq_restore(irq_flags);
    ++g_alloc_page_recursion_depth_bsp;
    int reclaim_result = paging_swap_reclaim_one_page();
    --g_alloc_page_recursion_depth_bsp;
    if (reclaim_result > 0) return alloc_contiguous_pages(page_count, align_pages);
    ++g_oom_pages;
    memory_report_oom("alloc_contiguous_pages", (uint64_t)page_count * PAGE_SIZE);
    return NULL;
}

void free_page(void *addr) {
    if (addr == NULL || g_page_bitmap == NULL) return;
    uint64_t page_num = (uint64_t)(uintptr_t)addr / PAGE_SIZE;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&page_lock);
    if (page_num < g_max_pages) {
        page_bitmap_set(page_num, 0u);
        if (page_num < (uint64_t)page_alloc_hint) page_alloc_hint = (uint32_t)page_num;
    }
    spinlock_unlock(&page_lock);
    irq_restore(irq_flags);
}

void free_contiguous_pages(void *addr, uint32_t page_count) {
    if (addr == NULL || page_count == 0 || g_page_bitmap == NULL) return;
    uint64_t start_page = (uint64_t)(uintptr_t)addr / PAGE_SIZE;
    if (start_page >= g_max_pages) return;
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&page_lock);
    uint64_t max_pages_left = g_max_pages - start_page;
    if ((uint64_t)page_count > max_pages_left) page_count = (uint32_t)max_pages_left;
    for (uint32_t i = 0; i < page_count; ++i) page_bitmap_set(start_page + i, 0u);
    if (start_page < (uint64_t)page_alloc_hint) page_alloc_hint = (uint32_t)start_page;
    spinlock_unlock(&page_lock);
    irq_restore(irq_flags);
}

void memory_dump_virtual(const void *addr, uint32_t bytes) {
    if (addr == NULL || bytes == 0) return;
    (void)addr; (void)bytes;
}

void memory_dump_physical(uint64_t phys_addr, uint32_t bytes) {
    if (bytes == 0) return;
    memory_dump_virtual((const void *)(uintptr_t)phys_addr, bytes);
}
