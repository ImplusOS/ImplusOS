#pragma once

#include <stdint.h>

/* ── EPT entry flags ──────────────────────────────────────────── */
#define EPT_READ        (1ULL << 0)
#define EPT_WRITE       (1ULL << 1)
#define EPT_EXECUTE     (1ULL << 2)
#define EPT_RWX         (EPT_READ | EPT_WRITE | EPT_EXECUTE)

/* Memory type in EPT PTEs (bits 5:3) */
#define EPT_MEMTYPE_UC  (0ULL << 3)
#define EPT_MEMTYPE_WC  (1ULL << 3)
#define EPT_MEMTYPE_WT  (4ULL << 3)
#define EPT_MEMTYPE_WP  (5ULL << 3)
#define EPT_MEMTYPE_WB  (6ULL << 3)

/* EPTP (EPT Pointer) fields */
#define EPTP_MEMTYPE_UC (0ULL)
#define EPTP_MEMTYPE_WB (6ULL)
#define EPTP_PAGE_WALK_4 (3ULL << 3)  /* Page-walk length - 1 = 3 (4-level) */

#define EPT_ADDR_MASK   0x000FFFFFFFFFF000ULL
#define EPT_PAGE_SIZE   4096ULL

/* ── EPT structure ────────────────────────────────────────────── */
typedef struct vmx_ept {
    uint64_t *pml4;           /* EPT PML4 table (page-aligned) */
    uint64_t  eptp;           /* EPT Pointer value for VMCS */
} vmx_ept_t;

/* ── API ──────────────────────────────────────────────────────── */

/**
 * Create a new EPT structure.
 * Returns pointer to ept, or NULL on failure.
 */
vmx_ept_t *ept_create(void);

/**
 * Map a single 4KB page: guest_phys -> host_phys with given flags.
 * Returns 0 on success, <0 on error.
 */
int ept_map_page(vmx_ept_t *ept,
                 uint64_t guest_phys,
                 uint64_t host_phys,
                 uint64_t flags);

/**
 * Map a contiguous range of pages.
 * Both guest_phys and host_phys must be page-aligned, size is in bytes.
 * Returns 0 on success, <0 on error.
 */
int ept_map_range(vmx_ept_t *ept,
                  uint64_t guest_phys,
                  uint64_t host_phys,
                  uint64_t size,
                  uint64_t flags);

/**
 * Destroy the EPT and free all page tables.
 */
void ept_destroy(vmx_ept_t *ept);
