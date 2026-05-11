#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "DMA_Memory.h"
#include "Memory_Main.h"
#include "Core/sync/Spinlock.h"
#include "mmu/Paging_Main.h"

#ifndef DMA_POOL_SIZE
#define DMA_POOL_SIZE   (8u * 1024u * 1024u)
#endif
#define DMA_MIN_ALIGN   64u
#define DMA_MAX_BLOCKS  2048u
#ifndef VIRT_TO_PHYS_OFFSET
#define VIRT_TO_PHYS_OFFSET  0ULL
#endif

typedef struct {
    uintptr_t virt;
    size_t    size;
    bool      used;
} dma_block_t;

typedef struct {
    uint8_t     *base_virt;
    uint64_t     base_phys;
    size_t       total;
    size_t       offset;
    dma_block_t  blocks[DMA_MAX_BLOCKS];
    uint32_t     block_cnt;
    bool         initialized;
} dma_pool_t;

static dma_pool_t g_pool;
static spinlock_t g_lock;

static bool dma_init_locked(void)
{
    if (g_pool.initialized) return true;

    size_t num_pages = (DMA_POOL_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;

    void *virt = pmm_alloc_pages(num_pages);
    if (!virt) {
        return false;
    }

    memset(virt, 0, num_pages * PAGE_SIZE);

    g_pool.base_virt   = (uint8_t*)virt;
    g_pool.base_phys   = (uint64_t)(uintptr_t)virt + VIRT_TO_PHYS_OFFSET;
    g_pool.total       = num_pages * PAGE_SIZE;
    g_pool.offset      = 0;
    g_pool.block_cnt   = 0;
    g_pool.initialized = true;

    return true;
}

bool dma_init(void)
{
    spinlock_lock(&g_lock);
    bool ok = dma_init_locked();
    spinlock_unlock(&g_lock);
    return ok;
}

static void dma_coalesce_locked(void)
{
    bool merged = true;
    while (merged) {
        merged = false;
        for (uint32_t i = 0; i < g_pool.block_cnt; i++) {
            dma_block_t *a = &g_pool.blocks[i];
            if (a->used) continue;
            for (uint32_t j = 0; j < g_pool.block_cnt; j++) {
                if (i == j) continue;
                dma_block_t *b = &g_pool.blocks[j];
                if (b->used) continue;
                if (a->virt + a->size == b->virt) {
                    a->size += b->size;
                    *b = g_pool.blocks[--g_pool.block_cnt];
                    merged = true;
                    break;
                }
            }
            if (merged) break;
        }
    }
}

void* dma_alloc(size_t bytes, uint64_t *phys_out)
{
    if (!bytes) return NULL;

    spinlock_lock(&g_lock);

    if (!g_pool.initialized) {
        if (!dma_init_locked()) {
            spinlock_unlock(&g_lock);
            return NULL;
        }
    }

    size_t need_align =
        (bytes >= (size_t)PAGE_SIZE) ? (size_t)PAGE_SIZE : (size_t)DMA_MIN_ALIGN;

    if (bytes > SIZE_MAX - need_align + 1) {
        spinlock_unlock(&g_lock);
        return NULL;
    }

    size_t aligned_bytes = (bytes + need_align - 1) & ~(need_align - 1);

    uint32_t best_idx  = DMA_MAX_BLOCKS;
    size_t   best_diff = SIZE_MAX;

    for (uint32_t i = 0; i < g_pool.block_cnt; i++) {
        dma_block_t *b = &g_pool.blocks[i];
        if (b->used) continue;
        if (b->size < aligned_bytes) continue;

        if ((b->virt & (need_align - 1)) != 0) continue;

        size_t diff = b->size - aligned_bytes;
        if (diff < best_diff) {
            best_diff = diff;
            best_idx  = i;
        }
    }

    if (best_idx < DMA_MAX_BLOCKS) {
        dma_block_t *b = &g_pool.blocks[best_idx];
        b->used = true;
        uintptr_t reuse_virt = b->virt;
        size_t    reuse_size = b->size;

        uint64_t phys =
            g_pool.base_phys + (reuse_virt - (uintptr_t)g_pool.base_virt);
        if (phys_out) *phys_out = phys;

        spinlock_unlock(&g_lock);
        memset((void*)reuse_virt, 0, reuse_size);
        return (void*)reuse_virt;
    }

    if (g_pool.block_cnt >= DMA_MAX_BLOCKS) {
        spinlock_unlock(&g_lock);
        return NULL;
    }

    size_t aligned_offset =
        (g_pool.offset + need_align - 1) & ~(need_align - 1);

    if (aligned_offset + aligned_bytes > g_pool.total) {
        spinlock_unlock(&g_lock);
        return NULL;
    }

    uint8_t  *virt = g_pool.base_virt + aligned_offset;
    uint64_t  phys = g_pool.base_phys + aligned_offset;

    dma_block_t *b = &g_pool.blocks[g_pool.block_cnt++];
    b->virt = (uintptr_t)virt;
    b->size = aligned_bytes;
    b->used = true;

    g_pool.offset = aligned_offset + aligned_bytes;

    if (phys_out) *phys_out = phys;

    spinlock_unlock(&g_lock);
    memset(virt, 0, aligned_bytes);
    return virt;
}

void dma_free(void *virt, size_t bytes)
{
    if (!virt || !g_pool.initialized) return;

    spinlock_lock(&g_lock);

    uintptr_t addr = (uintptr_t)virt;
    for (uint32_t i = 0; i < g_pool.block_cnt; i++) {
        dma_block_t *b = &g_pool.blocks[i];
        if (b->virt == addr && b->used) {
            if (bytes != 0 && bytes > b->size) {
                spinlock_unlock(&g_lock);
                return;
            }

            b->used = false;
            size_t clear_size = b->size;
            memset(virt, 0, clear_size);

            dma_coalesce_locked();

            spinlock_unlock(&g_lock);
            return;
        }
    }

    spinlock_unlock(&g_lock);
}

uint64_t virt_to_phys(void *virt)
{
    if (virt == NULL) {
        return 0;
    }

    uintptr_t virt_addr = (uintptr_t)virt;

    if (g_pool.initialized &&
        virt_addr >= (uintptr_t)g_pool.base_virt &&
        virt_addr < (uintptr_t)g_pool.base_virt + g_pool.total) {
        return g_pool.base_phys + (virt_addr - (uintptr_t)g_pool.base_virt);
    }

    return virt_addr + VIRT_TO_PHYS_OFFSET;
}

void dma_dump_stats(void)
{
    if (!g_pool.initialized) {
        return;
    }

    spinlock_lock(&g_lock);

    uint32_t used_blocks = 0;
    uint32_t free_blocks = 0;
    size_t   used_bytes  = 0;
    size_t   free_bytes  = 0;

    for (uint32_t i = 0; i < g_pool.block_cnt; i++) {
        dma_block_t *b = &g_pool.blocks[i];
        if (b->used) {
            used_blocks++;
            used_bytes += b->size;
        } else {
            free_blocks++;
            free_bytes += b->size;
        }
    }

    spinlock_unlock(&g_lock);
}