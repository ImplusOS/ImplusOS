#pragma once

#include <stdint.h>

#define EPT_READ        (1ULL << 0)
#define EPT_WRITE       (1ULL << 1)
#define EPT_EXECUTE     (1ULL << 2)
#define EPT_RWX         (EPT_READ | EPT_WRITE | EPT_EXECUTE)

#define EPT_MEMTYPE_UC  (0ULL << 3)
#define EPT_MEMTYPE_WC  (1ULL << 3)
#define EPT_MEMTYPE_WT  (4ULL << 3)
#define EPT_MEMTYPE_WP  (5ULL << 3)
#define EPT_MEMTYPE_WB  (6ULL << 3)

#define EPTP_MEMTYPE_UC (0ULL)
#define EPTP_MEMTYPE_WB (6ULL)
#define EPTP_PAGE_WALK_4 (3ULL << 3)  

#define EPT_ADDR_MASK   0x000FFFFFFFFFF000ULL
#define EPT_PAGE_SIZE   4096ULL
typedef struct vmx_ept {
    uint64_t *pml4;
    uint64_t  eptp;
} vmx_ept_t;

vmx_ept_t *ept_create(void);
int ept_map_page(vmx_ept_t *ept,
                 uint64_t guest_phys,
                 uint64_t host_phys,
                 uint64_t flags);
int ept_map_range(vmx_ept_t *ept,
                  uint64_t guest_phys,
                  uint64_t host_phys,
                  uint64_t size,
                  uint64_t flags);
void ept_destroy(vmx_ept_t *ept);
