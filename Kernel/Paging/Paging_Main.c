#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "Paging_Main.h"
#include "../Memory/Memory_Main.h"
#include "../ProcessManager/ProcessManager.h"
#include "../Sync/Spinlock.h"
#include "../KernelConfig.h"
#include "../Debbuger/Serial/Serial.h"
#include "../Debbuger/printf/printf.h"

#include <stddef.h>
#include <stdint.h>

#define GB (1024ULL * 1024ULL * 1024ULL)
#define MB2 (2ULL * 1024ULL * 1024ULL)
#define MAX_PDPT_ENTRIES 512
#define MMIO_WINDOW_BASE 0x00000000F0000000ULL
#define MMIO_WINDOW_SLOTS 512
#define PAGING_BOOT_IDENTITY_GB 4ULL
#define SWAP_TRACK_MAX 4096
#define PAGE_SWAP (1ULL << 9)

#define PAGE_SIZE_BYTES 4096ULL

typedef struct {
    uint8_t used;
    uint64_t cr3;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd_tables[MAX_PDPT_ENTRIES];
} paging_space_t;

static uint64_t g_kernel_pml4[512] __attribute__((aligned(4096)));
static uint64_t g_kernel_pdpt[512] __attribute__((aligned(4096)));
static uint64_t g_kernel_pd[MAX_PDPT_ENTRIES][512] __attribute__((aligned(4096)));
static uint64_t g_mmio_phys_base[MMIO_WINDOW_SLOTS];
static uint32_t g_mmio_slots_used = 0;
static uint64_t g_kernel_identity_entries = PAGING_BOOT_IDENTITY_GB;
static paging_space_t g_process_spaces_static[OS_CONFIG_PROCESS_MAX_COUNT];
static paging_space_t *g_process_spaces = g_process_spaces_static;
static uint32_t g_process_spaces_capacity = OS_CONFIG_PROCESS_MAX_COUNT;
static spinlock_t g_paging_space_lock;

typedef struct {
    uint8_t  used;
    uint8_t  reserved[7];
    void    *phys_page;
} swap_slot_t;

typedef struct {
    uint8_t used;
    uint8_t swapped;
    uint16_t reserved0;
    uint32_t slot_index;
    uint64_t cr3;
    uint64_t virt_addr;
} swap_track_t;

static swap_track_t g_swap_tracks[SWAP_TRACK_MAX];
static uint8_t g_swap_enabled = 1;
static spinlock_t g_swap_lock;

#define SWAP_SLOT_COUNT 128
static swap_slot_t g_swap_slots[SWAP_SLOT_COUNT];

static inline uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint64_t value)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"(value) : "memory");
}

static inline void enable_paging(void)
{
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 5);
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 31);
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}

static inline void invlpg_addr(uint64_t addr)
{
    __asm__ volatile ("invlpg (%0)" :: "r"(addr) : "memory");
}

#include "../SMP/SMP_Main.h"

static void tlb_shootdown_all(uint64_t vaddr, uint64_t pages)
{
    smp_tlb_shootdown(vaddr, pages);
}

static void copy_page_entries(uint64_t *dst, const uint64_t *src)
{
    for (uint32_t i = 0; i < 512; ++i) {
        dst[i] = src[i];
    }
}

static void copy_page_bytes(uint8_t *dst, const uint8_t *src)
{
    for (uint64_t i = 0; i < PAGE_SIZE_BYTES; ++i) {
        dst[i] = src[i];
    }
}

static uint64_t *alloc_zeroed_page_table(void)
{
    uint64_t *table = (uint64_t *)alloc_page();
    if (table == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < 512; ++i) {
        table[i] = 0;
    }
    return table;
}

static paging_space_t *find_space_by_cr3(uint64_t cr3)
{
    for (uint32_t i = 0; i < g_process_spaces_capacity; ++i) {
        if (g_process_spaces[i].used && g_process_spaces[i].cr3 == cr3) {
            return &g_process_spaces[i];
        }
    }
    return NULL;
}

static paging_space_t *find_free_space_slot(void)
{
    for (uint32_t i = 0; i < g_process_spaces_capacity; ++i) {
        if (!g_process_spaces[i].used) {
            return &g_process_spaces[i];
        }
    }
    return NULL;
}

static int ensure_kernel_pdpt_entry(uint64_t pdpt_index)
{
    if (pdpt_index >= MAX_PDPT_ENTRIES) {
        return -1;
    }

    if ((g_kernel_pdpt[pdpt_index] & PAGE_PRESENT) != 0) {
        return 0;
    }

    uint64_t base = pdpt_index * GB;
    for (uint64_t j = 0; j < 512; ++j) {
        g_kernel_pd[pdpt_index][j] = (base + (j * MB2)) | PAGE_PRESENT | PAGE_RW | PAGE_PS;
    }
    g_kernel_pdpt[pdpt_index] = ((uint64_t)g_kernel_pd[pdpt_index]) | PAGE_PRESENT | PAGE_RW;

    if (pdpt_index + 1 > g_kernel_identity_entries) {
        g_kernel_identity_entries = pdpt_index + 1;
    }
    return 0;
}

static int pd_table_has_user_pages(const uint64_t *pd_table)
{
    for (uint32_t i = 0; i < 512; ++i) {
        if ((pd_table[i] & PAGE_PRESENT) != 0 &&
            (pd_table[i] & PAGE_USER) != 0) {
            return 1;
        }
    }
    return 0;
}

static int update_pdpt_user_flag(uint64_t cr3,
                                 uint64_t pdpt_index,
                                 uint64_t *pd_table)
{
    if (pdpt_index >= MAX_PDPT_ENTRIES || pd_table == NULL) {
        return -1;
    }

    uint64_t flags = PAGE_PRESENT | PAGE_RW;
    if (pd_table_has_user_pages(pd_table)) {
        flags |= PAGE_USER;
    }

    if (cr3 == (uint64_t)g_kernel_pml4) {
        g_kernel_pdpt[pdpt_index] = ((uint64_t)pd_table) | flags;
        return 0;
    }

    paging_space_t *space = find_space_by_cr3(cr3);
    if (space == NULL) {
        return -1;
    }
    space->pdpt[pdpt_index] = ((uint64_t)pd_table) | flags;
    return 0;
}

static uint64_t *resolve_pd_table(uint64_t cr3, uint64_t pdpt_index)
{
    if (pdpt_index >= MAX_PDPT_ENTRIES) {
        return NULL;
    }

    if (cr3 == (uint64_t)g_kernel_pml4) {
        if (ensure_kernel_pdpt_entry(pdpt_index) < 0) {
            return NULL;
        }
        return g_kernel_pd[pdpt_index];
    }

    paging_space_t *space = find_space_by_cr3(cr3);
    if (space == NULL) {
        return NULL;
    }

    if (space->pd_tables[pdpt_index] == NULL &&
        (space->pdpt[pdpt_index] & PAGE_PRESENT) != 0) {
        space->pd_tables[pdpt_index] =
            (uint64_t *)(uintptr_t)(space->pdpt[pdpt_index] & PAGE_MASK);
    }
    return space->pd_tables[pdpt_index];
}

static int resolve_user_page_entry(uint64_t cr3,
                                   uint64_t virt_addr,
                                   uint64_t **entry_out)
{
    if (cr3 == 0 || entry_out == NULL) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];
    if ((pml4e & PAGE_PRESENT) == 0 || (pml4e & PAGE_USER) == 0) {
        return -1;
    }

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & PAGE_MASK);
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];
    if ((pdpte & PAGE_PRESENT) == 0 || (pdpte & PAGE_USER) == 0) {
        return -1;
    }

    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & PAGE_MASK);
    uint64_t pde = pd[PD_INDEX(virt_addr)];
    if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_USER) == 0) {
        return -1;
    }

    if ((pde & PAGE_PS) != 0) {
        *entry_out = &pd[PD_INDEX(virt_addr)];
        return 0;
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PAGE_MASK);
    uint64_t *pte = &pt[PT_INDEX(virt_addr)];
    if ((*pte & PAGE_PRESENT) == 0) {
        if (((*pte & PAGE_SWAP) == 0) || ((*pte & PAGE_USER) == 0)) {
            return -1;
        }
        *entry_out = pte;
        return 0;
    }
    if ((*pte & PAGE_USER) == 0) {
        return -1;
    }

    *entry_out = pte;
    return 0;
}

static int is_kernel_table(const void *table)
{
    if (table == (const void *)g_kernel_pdpt) {
        return 1;
    }
    for (uint32_t i = 0; i < MAX_PDPT_ENTRIES; ++i) {
        if (table == (const void *)g_kernel_pd[i]) {
            return 1;
        }
    }
    return 0;
}

static int split_huge_page(uint64_t *pd, uint64_t pd_index)
{
    uint64_t pde = pd[pd_index];

    if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_PS) == 0) {
        return 0;
    }

    uint64_t *pt = alloc_zeroed_page_table();
    if (pt == NULL) {
        return -1;
    }

    uint64_t phys_base = pde & ~(MB2 - 1ULL);

    uint64_t inherit = pde & (PAGE_RW | PAGE_USER |
                              (1ULL << 3) |
                              (1ULL << 4) |
                              (1ULL << 8));

    for (uint64_t i = 0; i < 512; ++i) {
        pt[i] = (phys_base + i * PAGE_SIZE_BYTES) | PAGE_PRESENT | inherit;
    }

    pd[pd_index] = ((uint64_t)pt) | PAGE_PRESENT | PAGE_RW |
                   (pde & PAGE_USER);

    return 0;
}

static int resolve_user_pte_slot(uint64_t cr3,
                                 uint64_t virt_addr,
                                 uint64_t **pte_out)
{
    if (cr3 == 0 || pte_out == NULL) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];
    if ((pml4e & PAGE_PRESENT) == 0 || (pml4e & PAGE_USER) == 0) {
        return -1;
    }

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & PAGE_MASK);
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];
    if ((pdpte & PAGE_PRESENT) == 0 || (pdpte & PAGE_USER) == 0) {
        return -1;
    }

    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & PAGE_MASK);
    uint64_t pde = pd[PD_INDEX(virt_addr)];
    if ((pde & PAGE_PRESENT) == 0) {
        return -1;
    }

    if ((pde & PAGE_USER) == 0) {
        return -1;
    }

    if ((pde & PAGE_PS) != 0) {
        *pte_out = &pd[PD_INDEX(virt_addr)];
        return 0;
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PAGE_MASK);
    *pte_out = &pt[PT_INDEX(virt_addr)];
    return 0;
}

static int is_user_virtual_address(uint64_t virt_addr)
{
    return ((virt_addr >= USER_CODE_BASE && virt_addr < USER_CODE_LIMIT) ||
            (virt_addr >= USER_HEAP_BASE && virt_addr < USER_HEAP_LIMIT) ||
            (virt_addr >= USER_STACK_BASE && virt_addr < USER_STACK_TOP));
}

static int resolve_fault_leaf_entry(uint64_t cr3,
                                    uint64_t virt_addr,
                                    uint64_t **pml4e_out,
                                    uint64_t **pdpte_out,
                                    uint64_t **pde_out,
                                    uint64_t **entry_out)
{
    if (cr3 == 0 || pml4e_out == NULL || pdpte_out == NULL ||
        pde_out == NULL || entry_out == NULL) {
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);
    uint64_t *pml4e = &pml4[PML4_INDEX(virt_addr)];
    if ((*pml4e & PAGE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(*pml4e & PAGE_MASK);
    uint64_t *pdpte = &pdpt[PDPT_INDEX(virt_addr)];
    if ((*pdpte & PAGE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pd = (uint64_t *)(uintptr_t)(*pdpte & PAGE_MASK);
    uint64_t *pde = &pd[PD_INDEX(virt_addr)];
    if ((*pde & PAGE_PRESENT) == 0) {
        return -1;
    }

    uint64_t *entry = NULL;
    if ((*pde & PAGE_PS) != 0) {
        entry = pde;
    } else {
        uint64_t *pt = (uint64_t *)(uintptr_t)(*pde & PAGE_MASK);
        entry = &pt[PT_INDEX(virt_addr)];
    }

    *pml4e_out = pml4e;
    *pdpte_out = pdpte;
    *pde_out = pde;
    *entry_out = entry;
    return 0;
}

static int swap_alloc_slot(void)
{
    spinlock_lock(&g_swap_lock);
    for (uint32_t i = 0; i < SWAP_SLOT_COUNT; ++i) {
        if (!g_swap_slots[i].used) {
            void *page = alloc_page();
            if (page == NULL) {
                spinlock_unlock(&g_swap_lock);
                return -1;
            }
            g_swap_slots[i].used       = 1;
            g_swap_slots[i].phys_page  = page;
            spinlock_unlock(&g_swap_lock);
            return (int)i;
        }
    }
    spinlock_unlock(&g_swap_lock);
    return -1;
}

static void swap_free_slot(uint32_t slot)
{
    if (slot >= SWAP_SLOT_COUNT) {
        return;
    }
    spinlock_lock(&g_swap_lock);
    if (g_swap_slots[slot].used && g_swap_slots[slot].phys_page != NULL) {
        free_page(g_swap_slots[slot].phys_page);
        g_swap_slots[slot].phys_page = NULL;
    }
    g_swap_slots[slot].used = 0;
    spinlock_unlock(&g_swap_lock);
}

static swap_track_t *swap_find_track(uint64_t cr3, uint64_t virt_addr)
{
    for (uint32_t i = 0; i < SWAP_TRACK_MAX; ++i) {
        if (g_swap_tracks[i].used &&
            g_swap_tracks[i].cr3 == cr3 &&
            g_swap_tracks[i].virt_addr == virt_addr) {
            return &g_swap_tracks[i];
        }
    }
    return NULL;
}

static void swap_forget_track(uint64_t cr3, uint64_t virt_addr)
{
    swap_track_t *track = swap_find_track(cr3, virt_addr);
    if (track == NULL) {
        return;
    }
    if (track->swapped) {
        swap_free_slot(track->slot_index);
    }
    track->used = 0;
    track->swapped = 0;
    track->slot_index = 0;
    track->cr3 = 0;
    track->virt_addr = 0;
}

static void swap_track_page(uint64_t cr3, uint64_t virt_addr)
{
    virt_addr &= PAGE_MASK;
    swap_track_t *existing = swap_find_track(cr3, virt_addr);
    if (existing != NULL) {
        if (existing->swapped) {
            swap_free_slot(existing->slot_index);
            existing->swapped = 0;
            existing->slot_index = 0;
        }
        existing->used = 1;
        return;
    }

    for (uint32_t i = 0; i < SWAP_TRACK_MAX; ++i) {
        if (!g_swap_tracks[i].used) {
            g_swap_tracks[i].used = 1;
            g_swap_tracks[i].swapped = 0;
            g_swap_tracks[i].slot_index = 0;
            g_swap_tracks[i].cr3 = cr3;
            g_swap_tracks[i].virt_addr = virt_addr;
            return;
        }
    }
}

static void paging_identity_mark_2mb_uncached(uint64_t phys_addr)
{
    uint64_t base = phys_addr & ~(MB2 - 1ULL);
    uint64_t pdpt_index = base / GB;
    uint64_t pd_index = (base - pdpt_index * GB) / MB2;

    if (pdpt_index >= MAX_PDPT_ENTRIES) {
        return;
    }
    if (ensure_kernel_pdpt_entry(pdpt_index) < 0) {
        return;
    }
    g_kernel_pd[pdpt_index][pd_index] |= PAGE_PCD | PAGE_PWT;
    invlpg_addr(base);
}

void *map_mmio_virt(uint64_t phys_addr)
{
    if (phys_addr >= 0x10000000000ULL) {
        return NULL;
    }

    if (phys_addr < (4ULL * GB)) {
        paging_identity_mark_2mb_uncached(phys_addr);
        return (void *)(uintptr_t)phys_addr;
    }

    uint64_t phys_base = phys_addr & ~(MB2 - 1ULL);
    uint64_t offset = phys_addr - phys_base;

    for (uint32_t i = 0; i < g_mmio_slots_used; ++i) {
        if (g_mmio_phys_base[i] == phys_base) {
            uint64_t virt_base = MMIO_WINDOW_BASE + ((uint64_t)i * MB2);
            return (void *)(uintptr_t)(virt_base + offset);
        }
    }

    if (g_mmio_slots_used >= MMIO_WINDOW_SLOTS) {
        return NULL;
    }

    uint64_t virt_base = MMIO_WINDOW_BASE + ((uint64_t)g_mmio_slots_used * MB2);
    uint64_t pdpt_index = (virt_base >> 30) & 0x1FFULL;
    uint64_t pd_index = (virt_base >> 21) & 0x1FFULL;

    if (pdpt_index >= MAX_PDPT_ENTRIES) {
        return NULL;
    }

    if ((g_kernel_pdpt[pdpt_index] & PAGE_PRESENT) == 0) {
        g_kernel_pdpt[pdpt_index] = ((uint64_t)g_kernel_pd[pdpt_index]) | PAGE_PRESENT | PAGE_RW;
    }

    g_kernel_pd[pdpt_index][pd_index] = phys_base | PAGE_PRESENT | PAGE_RW | PAGE_PS | PAGE_PCD | PAGE_PWT;
    
    if (pdpt_index + 1 > g_kernel_identity_entries) {
        g_kernel_identity_entries = pdpt_index + 1;
    }
    
    invlpg_addr(virt_base);

    g_mmio_phys_base[g_mmio_slots_used] = phys_base;
    g_mmio_slots_used++;

    return (void *)(uintptr_t)(virt_base + offset);
}

void init_paging(void)
{
    uint32_t efer_low, efer_high;
    uint32_t msr = 0xC0000080;
    __asm__ volatile ("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(msr));
    efer_low |= (1 << 11); 
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(efer_low), "d"(efer_high));

    memset(g_kernel_pml4, 0, sizeof(g_kernel_pml4));
    memset(g_kernel_pdpt, 0, sizeof(g_kernel_pdpt));
    memset(g_kernel_pd, 0, sizeof(g_kernel_pd));
    memset(g_mmio_phys_base, 0, sizeof(g_mmio_phys_base));
    memset(g_swap_slots, 0, sizeof(g_swap_slots));
    memset(g_swap_tracks, 0, sizeof(g_swap_tracks));
    spinlock_init(&g_paging_space_lock);
    spinlock_init(&g_swap_lock);

    for (uint32_t i = 0; i < g_process_spaces_capacity; ++i) {
        g_process_spaces[i].used = 0;
        g_process_spaces[i].cr3  = 0;
        g_process_spaces[i].pml4 = NULL;
        g_process_spaces[i].pdpt = NULL;
        for (uint32_t j = 0; j < MAX_PDPT_ENTRIES; ++j) {
            g_process_spaces[i].pd_tables[j] = NULL;
        }
    }
    g_mmio_slots_used = 0;
    g_kernel_identity_entries = PAGING_BOOT_IDENTITY_GB;
    uint64_t total_pages = get_total_memory_pages();
    if (total_pages > 0) {
        uint64_t total_bytes = total_pages * PAGE_SIZE_BYTES;
        uint64_t required_entries = (total_bytes + GB - 1ULL) / GB;
        if (required_entries > g_kernel_identity_entries) {
            g_kernel_identity_entries = required_entries;
        }
    }
    if (g_kernel_identity_entries > 256) {
        g_kernel_identity_entries = 256;
    }

    g_kernel_pml4[0] = ((uint64_t)g_kernel_pdpt) | PAGE_PRESENT | PAGE_RW;

    for (uint64_t i = 0; i < g_kernel_identity_entries; ++i) {
        if (ensure_kernel_pdpt_entry(i) < 0) {
            break;
        }
    }

    write_cr3((uint64_t)g_kernel_pml4);
}

uint64_t paging_get_kernel_cr3(void)
{
    return (uint64_t)g_kernel_pml4;
}

uint64_t paging_get_active_cr3(void)
{
    return read_cr3();
}

uint64_t paging_virt_to_phys(uint64_t cr3, uint64_t virt_addr)
{
    if (cr3 == 0) {
        return 0;
    }

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];
    if ((pml4e & PAGE_PRESENT) == 0) {
        return 0;
    }

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4e & PAGE_MASK);
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];
    if ((pdpte & PAGE_PRESENT) == 0) {
        return 0;
    }
    
    if ((pdpte & PAGE_PS) != 0) {
        uint64_t phys_base = pdpte & ~(GB - 1ULL);
        return phys_base | (virt_addr & (GB - 1ULL));
    }

    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpte & PAGE_MASK);
    uint64_t pde = pd[PD_INDEX(virt_addr)];
    if ((pde & PAGE_PRESENT) == 0) {
        return 0;
    }

    if ((pde & PAGE_PS) != 0) {
        uint64_t phys_base = pde & ~(MB2 - 1ULL);
        return phys_base | (virt_addr & (MB2 - 1ULL));
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PAGE_MASK);
    uint64_t pte = pt[PT_INDEX(virt_addr)];
    if ((pte & PAGE_PRESENT) == 0) {
        return 0;
    }

    return (pte & PAGE_MASK) | (virt_addr & (PAGE_SIZE_BYTES - 1ULL));
}

void paging_switch_cr3(uint64_t cr3)
{
    if (cr3 == 0) {
        return;
    }
    write_cr3(cr3);
}

uint64_t paging_create_process_space(void)
{
    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_paging_space_lock);
    paging_space_t *space = find_free_space_slot();
    if (space == NULL) {
        spinlock_unlock(&g_paging_space_lock);
        irq_restore(irq_flags);
        return 0;
    }
    memset(space, 0, sizeof(*space));

    space->used = 1;
    space->cr3 = UINT64_MAX;
    paging_space_t *space_ptr = space;
    spinlock_unlock(&g_paging_space_lock);
    irq_restore(irq_flags);

    space_ptr->pml4 = alloc_zeroed_page_table();
    if (!space_ptr->pml4) {
        uint64_t err_irq = irq_save_disable();
        spinlock_lock(&g_paging_space_lock);
        space_ptr->used = 0;
        space_ptr->cr3  = 0;
        spinlock_unlock(&g_paging_space_lock);
        irq_restore(err_irq);
        return 0;
    }

    copy_page_entries(space_ptr->pml4, g_kernel_pml4);

    space_ptr->pdpt = alloc_zeroed_page_table(); if (!space_ptr->pdpt) { uint64_t err_irq = irq_save_disable(); spinlock_lock(&g_paging_space_lock); free_page(space_ptr->pml4); space_ptr->used = 0; space_ptr->cr3 = 0; spinlock_unlock(&g_paging_space_lock); irq_restore(err_irq); return 0; } copy_page_entries(space_ptr->pdpt, g_kernel_pdpt);
    space_ptr->pml4[0] = ((uint64_t)(uintptr_t)space_ptr->pdpt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    for (uint64_t i = 0; i < MAX_PDPT_ENTRIES; ++i) {
        space_ptr->pd_tables[i] = NULL;
    }

    uint64_t irq_flags_final = irq_save_disable();
    spinlock_lock(&g_paging_space_lock);
    space_ptr->cr3 = (uint64_t)space_ptr->pml4;
    spinlock_unlock(&g_paging_space_lock);
    irq_restore(irq_flags_final);
    return space_ptr->cr3;
}

void paging_destroy_process_space(uint64_t cr3)
{
    if (cr3 == 0 || cr3 == (uint64_t)g_kernel_pml4) {
        return;
    }

    uint64_t irq_flags = irq_save_disable();
    spinlock_lock(&g_paging_space_lock);
    paging_space_t *space = find_space_by_cr3(cr3);
    if (space == NULL) {
        spinlock_unlock(&g_paging_space_lock);
        irq_restore(irq_flags);
        return;
    }
    paging_space_t space_copy = *space;
    space->used = 0;
    space->cr3 = 0;
    spinlock_unlock(&g_paging_space_lock);
    irq_restore(irq_flags);

    if (read_cr3() == cr3) {
        write_cr3((uint64_t)g_kernel_pml4);
    }
    
    tlb_shootdown_all(0, g_kernel_identity_entries * (GB / PAGE_SIZE_BYTES));

    for (uint64_t i = 0; i < MAX_PDPT_ENTRIES; ++i) {
        if (space_copy.pd_tables[i] == NULL) {
            continue;
        }

        for (uint64_t j = 0; j < 512; ++j) {
            uint64_t pde = space_copy.pd_tables[i][j];
            if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_PS) != 0) {
                continue;
            }

            uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PAGE_MASK);
            for (uint64_t k = 0; k < 512; ++k) {
                uint64_t pte = pt[k];
                if ((pte & PAGE_USER) == 0) {
                    continue;
                }
                uint64_t virt_addr = (i << 30) | (j << 21) | (k << 12);
                if ((pte & PAGE_PRESENT) != 0) {
                    free_page((void *)(uintptr_t)(pte & PAGE_MASK));
                } else if ((pte & PAGE_SWAP) != 0) {
                    uint32_t slot = (uint32_t)((pte & PAGE_MASK) >> 12);
                    swap_free_slot(slot);
                }
                swap_forget_track(cr3, virt_addr);
                pt[k] = 0;
            }
            free_page(pt);
        }

        free_page(space_copy.pd_tables[i]);
    }
    if (space_copy.pdpt != NULL && space_copy.pdpt != g_kernel_pdpt) {
        free_page(space_copy.pdpt);
    }
    if (space_copy.pml4 != NULL) {
        free_page(space_copy.pml4);
    }

    for (uint32_t i = 0; i < SWAP_TRACK_MAX; ++i) {
        if (g_swap_tracks[i].used && g_swap_tracks[i].cr3 == cr3) {
            swap_forget_track(cr3, g_swap_tracks[i].virt_addr);
        }
    }
}

int paging_set_user_access(uint64_t cr3,
                           uint64_t start,
                           uint64_t size,
                           int enable_user)
{
    if (cr3 == 0 || size == 0)
        return -1;

    uint64_t end = start + size;
    if (end <= start)
        return -1;

    uint64_t aligned_start = start & ~(PAGE_SIZE_BYTES - 1ULL);
    uint64_t aligned_end   = (end + PAGE_SIZE_BYTES - 1ULL) & ~(PAGE_SIZE_BYTES - 1ULL);
    if (aligned_end < end) return -1;

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE_BYTES)
    {
        uint64_t pml4_index = (addr >> 39) & 0x1FFULL;
        uint64_t pdpt_index = (addr >> 30) & 0x1FFULL;
        uint64_t pd_index   = (addr >> 21) & 0x1FFULL;
        uint64_t pt_index   = (addr >> 12) & 0x1FFULL;

        if ((pml4[pml4_index] & PAGE_PRESENT) == 0) {
            return -1;
        }
        if (enable_user) {
            pml4[pml4_index] |= PAGE_USER;
        }

        uint64_t *pd_table = resolve_pd_table(cr3, pdpt_index);
        if (pd_table == NULL) {
            if (cr3 == (uint64_t)g_kernel_pml4) {
                if (ensure_kernel_pdpt_entry(pdpt_index) < 0) {
                    return -1;
                }
                pd_table = g_kernel_pd[pdpt_index];
            } else {
                uint64_t irq_f = irq_save_disable();
                spinlock_lock(&g_paging_space_lock);
                paging_space_t *space = find_space_by_cr3(cr3);
                if (space == NULL) {
                    spinlock_unlock(&g_paging_space_lock);
                    irq_restore(irq_f);
                    return -1;
                }
                pd_table = alloc_zeroed_page_table();
                if (pd_table == NULL) {
                    spinlock_unlock(&g_paging_space_lock);
                    irq_restore(irq_f);
                    return -1;
                }
                space->pd_tables[pdpt_index] = pd_table;
                space->pdpt[pdpt_index] = ((uint64_t)pd_table) | PAGE_PRESENT | PAGE_RW;
                if (enable_user) {
                    space->pdpt[pdpt_index] |= PAGE_USER;
                }
                spinlock_unlock(&g_paging_space_lock);
                irq_restore(irq_f);
            }
        }

        if ((pd_table[pd_index] & PAGE_PRESENT) != 0 &&
            (pd_table[pd_index] & PAGE_PS) != 0) {
            if (split_huge_page(pd_table, pd_index) < 0) {
                return -1;
            }
        }

        if ((pd_table[pd_index] & PAGE_PRESENT) == 0) {
            if (!enable_user) {
                if (update_pdpt_user_flag(cr3, pdpt_index, pd_table) < 0) {
                    return -1;
                }
                continue;
            }
            
            uint64_t *pt = alloc_zeroed_page_table();
            if (pt == NULL) {
                return -1;
            }

            void *phys_page = alloc_page();
            if (phys_page == NULL) {
                free_page(pt);
                return -1;
            }
            memset(phys_page, 0, PAGE_SIZE_BYTES);

            pt[pt_index] = ((uint64_t)(uintptr_t)phys_page) |
                           PAGE_PRESENT |
                           PAGE_RW |
                           PAGE_USER;

            pd_table[pd_index] = ((uint64_t)pt) |
                                 PAGE_PRESENT |
                                 PAGE_RW |
                                 PAGE_USER;

        } else {
            uint64_t *pt = (uint64_t *)(uintptr_t)(pd_table[pd_index] & PAGE_MASK);
            uint64_t pte = pt[pt_index];

            if ((pte & PAGE_PRESENT) == 0 && (pte & PAGE_SWAP) == 0) {
                if (enable_user) {
                    void *phys_page = alloc_page();
                    if (phys_page == NULL) {
                        return -1;
                    }
                    memset(phys_page, 0, PAGE_SIZE_BYTES);
                    pte = ((uint64_t)(uintptr_t)phys_page) |
                          PAGE_PRESENT |
                          PAGE_RW |
                          PAGE_USER;
                    pt[pt_index] = pte;
                }
            } else {
                if (enable_user) {
                    pte |= PAGE_USER;
                    pte |= PAGE_RW;
                } else {
                    pte &= ~PAGE_USER;
                }
                pt[pt_index] = pte;
            }

            if (enable_user) {
                pd_table[pd_index] |= PAGE_RW;
                pd_table[pd_index] |= PAGE_USER;
            } else {
                int any_user = 0;
                for (uint32_t k = 0; k < 512; ++k) {
                    if (((pt[k] & PAGE_PRESENT) != 0 ||
                         (pt[k] & PAGE_SWAP) != 0) &&
                        (pt[k] & PAGE_USER) != 0) {
                        any_user = 1;
                        break;
                    }
                }
                if (!any_user) {
                    pd_table[pd_index] &= ~PAGE_USER;
                }
            }
        }

        if (update_pdpt_user_flag(cr3, pdpt_index, pd_table) < 0) {
            return -1;
        }
    }

    if (read_cr3() == cr3)
        write_cr3(cr3);

    return 0;
}

int paging_unmap_range(uint64_t cr3, uint64_t start, uint64_t size)
{
    if (cr3 == 0 || size == 0) {
        return -1;
    }

    uint64_t end = start + size;
    if (end <= start) {
        return -1;
    }

    uint64_t aligned_start = start & ~(PAGE_SIZE_BYTES - 1ULL);
    uint64_t aligned_end = (end + PAGE_SIZE_BYTES - 1ULL) & ~(PAGE_SIZE_BYTES - 1ULL);
    if (aligned_end < end) return -1;

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE_BYTES) {
        uint64_t pdpt_index = (addr >> 30) & 0x1FFULL;
        uint64_t pd_index = (addr >> 21) & 0x1FFULL;
        uint64_t pt_index = (addr >> 12) & 0x1FFULL;

        uint64_t *pd_table = resolve_pd_table(cr3, pdpt_index);
        if (pd_table == NULL) {
            continue;
        }

        uint64_t pde = pd_table[pd_index];
        if ((pde & PAGE_PRESENT) == 0) {
            continue;
        }

        if ((pde & PAGE_PS) != 0) {
            if (split_huge_page(pd_table, pd_index) < 0) {
                return -1;
            }
            pde = pd_table[pd_index];
        }

        if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_PS) != 0) {
            continue;
        }

        uint64_t *pt = (uint64_t *)(uintptr_t)(pde & PAGE_MASK);
        uint64_t old_pte = pt[pt_index];
        if ((old_pte & PAGE_SWAP) != 0 && (old_pte & PAGE_PRESENT) == 0) {
            uint32_t slot = (uint32_t)((old_pte & PAGE_MASK) >> 12);
            swap_free_slot(slot);
        } else if ((old_pte & PAGE_PRESENT) != 0 && (old_pte & PAGE_USER) != 0) {
            if ((old_pte & PAGE_EXTERNAL) == 0) {
                free_page((void *)(uintptr_t)(old_pte & PAGE_MASK));
            }
        }
        swap_forget_track(cr3, addr);
        pt[pt_index] = 0;

        int any_present = 0;
        for (uint64_t i = 0; i < 512; ++i) {
            if ((pt[i] & PAGE_PRESENT) != 0 || (pt[i] & PAGE_SWAP) != 0) {
                any_present = 1;
                break;
            }
        }

        if (!any_present) {
            free_page(pt);
            pd_table[pd_index] = 0;
        }
        if (update_pdpt_user_flag(cr3, pdpt_index, pd_table) < 0) {
            return -1;
        }
    }

    if (read_cr3() == cr3) {
        write_cr3(cr3);
    }
    return 0;
}

int paging_is_user_range_mapped(uint64_t cr3, uint64_t start, uint64_t size)
{
    if (size == 0) {
        return 1;
    }
    if (cr3 == 0) {
        return 0;
    }

    uint64_t end = start + size;
    if (end <= start) {
        return 0;
    }

    uint64_t aligned_start = start & ~(PAGE_SIZE_BYTES - 1ULL);
    uint64_t aligned_end = (end + PAGE_SIZE_BYTES - 1ULL) & ~(PAGE_SIZE_BYTES - 1ULL);
    if (aligned_end < end) return 0;

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE_BYTES) {
        uint64_t *entry = NULL;
        if (resolve_user_page_entry(cr3, addr, &entry) < 0 || entry == NULL) {
            return 0;
        }
    }

    return 1;
}

void *pmm_alloc_pages(size_t num_pages)
{
    return alloc_contiguous_pages(num_pages, 1);
}

int paging_map_user_page(uint64_t cr3,
                         uint64_t virt_addr,
                         uint64_t phys_addr,
                         uint64_t flags)
{
    if (cr3 == 0) {
        return -1;
    }

    uint64_t saved_cr3 = read_cr3();
    uint64_t kernel_cr3 = paging_get_kernel_cr3();

    if (saved_cr3 != kernel_cr3) {
        write_cr3(kernel_cr3);
    }

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);
    uint16_t i4 = PML4_INDEX(virt_addr);
    uint16_t i3 = PDPT_INDEX(virt_addr);
    uint16_t i2 = PD_INDEX(virt_addr);
    uint16_t i1 = PT_INDEX(virt_addr);

    uint64_t *pdpt = NULL;
    uint64_t *pd   = NULL;
    uint64_t *pt   = NULL;
    paging_space_t *space = find_space_by_cr3(cr3);

    if ((pml4[i4] & PAGE_PRESENT) == 0) {
        pdpt = alloc_zeroed_page_table();
        if (pdpt == NULL) {
            if (saved_cr3 != kernel_cr3) write_cr3(saved_cr3);
            return -1;
        }
        pml4[i4] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        if (space != NULL) {
            space->pdpt = pdpt;
        }
    } else {
        pdpt = (uint64_t *)(uintptr_t)(pml4[i4] & PAGE_MASK);

        if (is_kernel_table(pdpt)) {
            uint64_t *cloned = alloc_zeroed_page_table();
            if (cloned == NULL) {
                if (saved_cr3 != kernel_cr3) write_cr3(saved_cr3);
                return -1;
            }
            copy_page_entries(cloned, pdpt);
            pdpt = cloned;
            pml4[i4] = ((uint64_t)pdpt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

            if (space != NULL) {
                space->pdpt = cloned;
            }
        } else {
            pml4[i4] |= PAGE_USER;
        }
    }

    if ((pdpt[i3] & PAGE_PRESENT) == 0) {
        pd = alloc_zeroed_page_table();
        if (pd == NULL) {
            if (saved_cr3 != kernel_cr3) write_cr3(saved_cr3);
            return -1;
        }
        pdpt[i3] = ((uint64_t)pd) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    } else {
        pd = (uint64_t *)(uintptr_t)(pdpt[i3] & PAGE_MASK);

        if (is_kernel_table(pd)) {
            uint64_t *cloned = alloc_zeroed_page_table();
            if (cloned == NULL) {
                if (saved_cr3 != kernel_cr3) write_cr3(saved_cr3);
                return -1;
            }
            copy_page_entries(cloned, pd);
            pd = cloned;
            pdpt[i3] = ((uint64_t)pd) | PAGE_PRESENT | PAGE_RW | PAGE_USER;

            if (space != NULL && i3 < MAX_PDPT_ENTRIES) {
                space->pd_tables[i3] = cloned;
            }
        } else {
            pdpt[i3] |= PAGE_USER;
        }
    }
    if (space != NULL && i3 < MAX_PDPT_ENTRIES) {
        space->pd_tables[i3] = pd;
    }

    if ((pd[i2] & PAGE_PRESENT) != 0 && (pd[i2] & PAGE_PS) != 0) {
        if (split_huge_page(pd, i2) < 0) {
            if (saved_cr3 != kernel_cr3) write_cr3(saved_cr3);
            return -1;
        }
        pd[i2] |= PAGE_USER;
    }

    if ((pd[i2] & PAGE_PRESENT) == 0) {
        pt = alloc_zeroed_page_table();
        if (pt == NULL) {
            if (saved_cr3 != kernel_cr3) write_cr3(saved_cr3);
            return -1;
        }
        pd[i2] = ((uint64_t)pt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    } else {
        pd[i2] |= PAGE_USER;
        pt = (uint64_t *)(uintptr_t)(pd[i2] & PAGE_MASK);
    }

    uint64_t old_pte = pt[i1];
    if ((old_pte & PAGE_PRESENT) != 0 && (old_pte & PAGE_USER) != 0) {
        if ((old_pte & PAGE_EXTERNAL) == 0) {
            free_page((void *)(uintptr_t)(old_pte & PAGE_MASK));
        }
    } else if ((old_pte & PAGE_SWAP) != 0 && (old_pte & PAGE_USER) != 0) {
        uint32_t slot = (uint32_t)((old_pte & PAGE_MASK) >> 12);
        swap_free_slot(slot);
    }

    pt[i1] = (phys_addr & PAGE_MASK) |
             PAGE_PRESENT |
             PAGE_USER |
             (flags & (PAGE_RW | PAGE_PWT | PAGE_PCD | PAGE_EXTERNAL | PAGE_NX));
    swap_track_page(cr3, virt_addr);

    if (saved_cr3 != kernel_cr3) {
        write_cr3(saved_cr3);
    }
    
    if (cr3 == read_cr3()) {
        invlpg_addr(virt_addr & PAGE_MASK);
    }
    return 0;
}

int paging_map_user_range_alloc(uint64_t cr3,
                                uint64_t start,
                                uint64_t size,
                                uint64_t flags)
{
    if (cr3 == 0 || size == 0) {
        return -1;
    }

    uint64_t end = start + size;
    if (end <= start) {
        return -1;
    }

    uint64_t aligned_start = start & ~(PAGE_SIZE_BYTES - 1ULL);
    uint64_t aligned_end = (end + PAGE_SIZE_BYTES - 1ULL) & ~(PAGE_SIZE_BYTES - 1ULL);
    if (aligned_end < end) return -1;

    for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE_BYTES) {
        void *phys_page = alloc_page();
        if (phys_page == NULL) {
            return -1;
        }

        memset(phys_page, 0, PAGE_SIZE_BYTES);

        if (paging_map_user_page(cr3,
                                 addr,
                                 (uint64_t)(uintptr_t)phys_page,
                                 flags) < 0) {
            free_page(phys_page);
            return -1;
        }
    }

    return 0;
}

void paging_swap_set_enabled(int enable)
{
    g_swap_enabled = (enable != 0) ? 1u : 0u;
}

int paging_swap_reclaim_one_page(void)
{
    return 0;
}

int paging_handle_swap_fault(uint64_t cr3, uint64_t fault_addr)
{
    if (cr3 == 0) return 0;

    uint64_t virt_addr = fault_addr & PAGE_MASK;
    
    if (!is_user_virtual_address(virt_addr)) {
        return 0;
    }

    uint64_t *pml4e = NULL, *pdpte = NULL, *pde = NULL, *pte = NULL;
    int leaf_res = resolve_fault_leaf_entry(cr3, virt_addr, &pml4e, &pdpte, &pde, &pte);

    if (leaf_res < 0 || pte == NULL || ((*pte & PAGE_PRESENT) == 0 && (*pte & PAGE_SWAP) == 0)) {
        
        void *phys_page = alloc_page();
        if (phys_page == NULL) {
            return 0;
        }
        
        memset(phys_page, 0, PAGE_SIZE_BYTES);

        if (paging_map_user_page(cr3, virt_addr, (uint64_t)(uintptr_t)phys_page, PAGE_RW) < 0) {
            free_page(phys_page);
            return 0;
        }

        if (cr3 == read_cr3()) {
            invlpg_addr(virt_addr);
        }
        
        return 1;
    }

    int repaired_permissions = 0;
    if ((*pml4e & PAGE_USER) == 0) { *pml4e |= PAGE_USER; repaired_permissions = 1; }
    if ((*pdpte & PAGE_USER) == 0) { *pdpte |= PAGE_USER; repaired_permissions = 1; }
    if ((*pde & PAGE_USER) == 0)   { *pde |= PAGE_USER; repaired_permissions = 1; }

    uint64_t entry = *pte;
    if ((entry & PAGE_PRESENT) != 0) {
        if ((entry & PAGE_USER) == 0) { 
            *pte = entry | PAGE_USER; 
            repaired_permissions = 1; 
        }

        if (repaired_permissions) {
            if (cr3 == read_cr3()) invlpg_addr(virt_addr);
            return 1;
        }
        return 0;
    }

    if ((entry & PAGE_SWAP) == 0 || !g_swap_enabled) {
        return 0;
    }

    uint32_t slot = (uint32_t)((entry & PAGE_MASK) >> 12);
    if (slot >= SWAP_SLOT_COUNT || !g_swap_slots[slot].used) {
        return 0;
    }

    void *phys_page = alloc_page();
    if (phys_page == NULL) return 0;

    copy_page_bytes((uint8_t *)phys_page, g_swap_slots[slot].phys_page);
    swap_free_slot(slot);

    uint64_t flags = entry & PAGE_RW;
    *pte = ((uint64_t)(uintptr_t)phys_page) | PAGE_PRESENT | PAGE_USER | flags;

    swap_track_t *track = swap_find_track(cr3, virt_addr);
    if (track != NULL) {
        track->swapped = 0;
        track->slot_index = 0;
    } else {
        swap_track_page(cr3, virt_addr);
    }

    tlb_shootdown_all(virt_addr, 1ULL);
    return 1;
}

void pmm_free_pages(void *virt, size_t num_pages)
{
    if (virt == NULL || num_pages == 0) return;

    uint8_t *p = (uint8_t *)virt;
    for (size_t i = 0; i < num_pages; ++i) {
        free_page(p + i * PAGE_SIZE_BYTES);
    }
}

uint64_t get_phys_base(void)
{
    return 0ULL;
}

uint64_t get_virt_base(void)
{
    return 0ULL;
}