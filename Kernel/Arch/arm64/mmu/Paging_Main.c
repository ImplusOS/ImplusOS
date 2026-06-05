#include "Paging_Main.h"
#include "MemoryManagement/Memory_Main.h"
#include "interfaces/mmu_ops.h"

#define ARM64_DESC_VALID       (1ULL << 0)
#define ARM64_DESC_TABLE       (1ULL << 1)
#define ARM64_BLOCK_AF         (1ULL << 10)
#define ARM64_BLOCK_SH_INNER   (3ULL << 8)
#define ARM64_BLOCK_AP_RW_EL1  (0ULL << 6)
#define ARM64_BLOCK_ATTR_NORMAL (1ULL << 2)
#define ARM64_BLOCK_ATTR_DEVICE (0ULL << 2)
#define ARM64_BLOCK_PXN        (1ULL << 53)
#define ARM64_BLOCK_UXN        (1ULL << 54)
#define ARM64_L1_BLOCK_SIZE    0x40000000ULL
#define ARM64_TABLE_ENTRIES    512

static uint64_t g_l0[ARM64_TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t g_l1_identity[ARM64_TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint64_t g_kernel_ttbr = 0;

static inline void write_sysreg_ttbr0(uint64_t value) { __asm__ volatile("msr TTBR0_EL1, %0" :: "r"(value) : "memory"); }
static inline void write_sysreg_ttbr1(uint64_t value) { __asm__ volatile("msr TTBR1_EL1, %0" :: "r"(value) : "memory"); }
static inline void write_sysreg_tcr(uint64_t value) { __asm__ volatile("msr TCR_EL1, %0" :: "r"(value) : "memory"); }
static inline void write_sysreg_mair(uint64_t value) { __asm__ volatile("msr MAIR_EL1, %0" :: "r"(value) : "memory"); }
static inline uint64_t read_sctlr(void) { uint64_t v; __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(v)); return v; }
static inline void write_sctlr(uint64_t value) { __asm__ volatile("msr SCTLR_EL1, %0; isb" :: "r"(value) : "memory"); }

static uint64_t table_desc(uint64_t table)
{
    return (table & PAGE_MASK) | ARM64_DESC_TABLE | ARM64_DESC_VALID;
}

static uint64_t block_desc(uint64_t phys, int device)
{
    uint64_t attr = device ? ARM64_BLOCK_ATTR_DEVICE : ARM64_BLOCK_ATTR_NORMAL;
    return (phys & ~(ARM64_L1_BLOCK_SIZE - 1ULL)) |
           ARM64_DESC_VALID | ARM64_BLOCK_AF | ARM64_BLOCK_SH_INNER |
           ARM64_BLOCK_AP_RW_EL1 | attr | ARM64_BLOCK_PXN | ARM64_BLOCK_UXN;
}

void init_paging(void)
{
    for (uint64_t i = 0; i < ARM64_TABLE_ENTRIES; ++i) {
        g_l0[i] = 0;
        g_l1_identity[i] = block_desc(i * ARM64_L1_BLOCK_SIZE, 0);
    }

    g_l0[0] = table_desc((uint64_t)(uintptr_t)g_l1_identity);
    g_kernel_ttbr = (uint64_t)(uintptr_t)g_l0;

    write_sysreg_mair(0xFF00ULL);
    write_sysreg_tcr((16ULL << 0) | (16ULL << 16) | (0ULL << 14) | (2ULL << 30) |
                     (3ULL << 12) | (3ULL << 28) | (1ULL << 8) | (1ULL << 24));
    write_sysreg_ttbr0(g_kernel_ttbr);
    write_sysreg_ttbr1(g_kernel_ttbr);
    __asm__ volatile("dsb ish; isb; tlbi vmalle1; dsb ish; isb" ::: "memory");
    write_sctlr(read_sctlr() | 1ULL | (1ULL << 2) | (1ULL << 12));
}

void *map_mmio_virt(uint64_t phys_addr)
{
    return (void *)(uintptr_t)phys_addr;
}

uint64_t paging_get_kernel_cr3(void) { return g_kernel_ttbr; }
uint64_t paging_get_active_cr3(void) { return g_kernel_ttbr; }
void paging_switch_cr3(uint64_t address_space) { (void)address_space; }
uint64_t paging_create_process_space(void) { return g_kernel_ttbr; }
void paging_destroy_process_space(uint64_t address_space) { (void)address_space; }
int paging_set_user_access(uint64_t address_space, uint64_t start, uint64_t size, int enable_user) { (void)address_space; (void)start; (void)size; (void)enable_user; return 0; }
int paging_unmap_range(uint64_t address_space, uint64_t start, uint64_t size) { (void)address_space; (void)start; (void)size; return 0; }
int paging_is_user_range_mapped(uint64_t address_space, uint64_t start, uint64_t size) { (void)address_space; (void)start; (void)size; return 1; }
int paging_map_user_page(uint64_t address_space, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) { (void)address_space; (void)virt_addr; (void)phys_addr; (void)flags; return 0; }
int paging_map_user_range_alloc(uint64_t address_space, uint64_t start, uint64_t size, uint64_t flags) { (void)address_space; (void)start; (void)size; (void)flags; return 0; }
void paging_swap_set_enabled(int enable) { (void)enable; }
int paging_swap_reclaim_one_page(void) { return 0; }
int paging_handle_swap_fault(uint64_t address_space, uint64_t fault_addr) { (void)address_space; (void)fault_addr; return -1; }
void *pmm_alloc_pages(size_t num_pages) { return alloc_contiguous_pages((uint32_t)num_pages, 1); }
void pmm_free_pages(void *virt, size_t num_pages) { free_contiguous_pages(virt, (uint32_t)num_pages); }
uint64_t get_phys_base(void) { return 0; }
uint64_t get_virt_base(void) { return 0; }
uint64_t paging_virt_to_phys(uint64_t address_space, uint64_t virt_addr) { (void)address_space; return virt_addr; }

static const mmu_ops_t g_arm64_mmu_ops = {
    .init = init_paging,
    .map_mmio = map_mmio_virt,
    .kernel_address_space = paging_get_kernel_cr3,
    .active_address_space = paging_get_active_cr3,
    .switch_address_space = paging_switch_cr3,
    .create_address_space = paging_create_process_space,
    .destroy_address_space = paging_destroy_process_space,
    .map_user_page = paging_map_user_page,
    .map_user_range_alloc = paging_map_user_range_alloc,
    .unmap_range = paging_unmap_range,
    .virt_to_phys = paging_virt_to_phys,
};

const mmu_ops_t *mmu_ops_get(void)
{
    return &g_arm64_mmu_ops;
}
