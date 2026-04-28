#include "EPT.h"
#include "../../Memory/Memory_Main.h"
#include "../../Sync/Spinlock.h"
#include "../../Debbuger/printf/printf.h"
#include <string.h>
#include <stddef.h>

static uint64_t *ept_alloc_table(void)
{
    void *page = alloc_page();
    if (page == NULL) {
        return NULL;
    }
    memset(page, 0, EPT_PAGE_SIZE);
    return (uint64_t *)page;
}

static void ept_free_table(uint64_t *table)
{
    if (table != NULL) {
        free_page(table);
    }
}

#define EPT_PML4_INDEX(gpa) (((gpa) >> 39) & 0x1FFULL)
#define EPT_PDPT_INDEX(gpa) (((gpa) >> 30) & 0x1FFULL)
#define EPT_PD_INDEX(gpa)   (((gpa) >> 21) & 0x1FFULL)
#define EPT_PT_INDEX(gpa)   (((gpa) >> 12) & 0x1FFULL)

vmx_ept_t *ept_create(void)
{
    vmx_ept_t *ept = (vmx_ept_t *)malloc(sizeof(vmx_ept_t));
    if (ept == NULL) {
        return NULL;
    }

    ept->pml4 = ept_alloc_table();
    if (ept->pml4 == NULL) {
        free(ept);
        return NULL;
    }

    uint64_t pml4_phys = (uint64_t)(uintptr_t)ept->pml4;
    ept->eptp = pml4_phys | EPTP_MEMTYPE_WB | EPTP_PAGE_WALK_4;

    return ept;
}

int ept_map_page(vmx_ept_t *ept,
                 uint64_t guest_phys,
                 uint64_t host_phys,
                 uint64_t flags)
{
    if (ept == NULL || ept->pml4 == NULL) {
        return -1;
    }

    guest_phys &= EPT_ADDR_MASK;
    host_phys  &= EPT_ADDR_MASK;

    uint64_t pml4_idx = EPT_PML4_INDEX(guest_phys);
    uint64_t pdpt_idx = EPT_PDPT_INDEX(guest_phys);
    uint64_t pd_idx   = EPT_PD_INDEX(guest_phys);
    uint64_t pt_idx   = EPT_PT_INDEX(guest_phys);

    uint64_t *pml4e = &ept->pml4[pml4_idx];
    uint64_t *pdpt;
    if ((*pml4e & EPT_RWX) == 0) {
        pdpt = ept_alloc_table();
        if (pdpt == NULL) return -1;
        *pml4e = ((uint64_t)(uintptr_t)pdpt & EPT_ADDR_MASK) | EPT_RWX;
    } else {
        pdpt = (uint64_t *)(uintptr_t)(*pml4e & EPT_ADDR_MASK);
    }
    
    uint64_t *pdpte = &pdpt[pdpt_idx];
    uint64_t *pd;
    if ((*pdpte & EPT_RWX) == 0) {
        pd = ept_alloc_table();
        if (pd == NULL) return -1;
        *pdpte = ((uint64_t)(uintptr_t)pd & EPT_ADDR_MASK) | EPT_RWX;
    } else {
        pd = (uint64_t *)(uintptr_t)(*pdpte & EPT_ADDR_MASK);
    }

    uint64_t *pde = &pd[pd_idx];
    uint64_t *pt;
    if ((*pde & EPT_RWX) == 0) {
        pt = ept_alloc_table();
        if (pt == NULL) return -1;
        *pde = ((uint64_t)(uintptr_t)pt & EPT_ADDR_MASK) | EPT_RWX;
    } else {
        pt = (uint64_t *)(uintptr_t)(*pde & EPT_ADDR_MASK);
    }

    pt[pt_idx] = (host_phys & EPT_ADDR_MASK) | flags;

    return 0;
}

int ept_map_range(vmx_ept_t *ept,
                  uint64_t guest_phys,
                  uint64_t host_phys,
                  uint64_t size,
                  uint64_t flags)
{
    if (ept == NULL || size == 0) {
        return -1;
    }

    uint64_t offset = 0;
    while (offset < size) {
        int rc = ept_map_page(ept,
                              guest_phys + offset,
                              host_phys + offset,
                              flags);
        if (rc < 0) {
            return rc;
        }
        offset += EPT_PAGE_SIZE;
    }

    return 0;
}

static void ept_free_pt(uint64_t *pt)
{
    ept_free_table(pt);
}

static void ept_free_pd(uint64_t *pd)
{
    if (pd == NULL) return;
    for (uint64_t i = 0; i < 512; i++) {
        if ((pd[i] & EPT_RWX) != 0) {
            uint64_t *pt = (uint64_t *)(uintptr_t)(pd[i] & EPT_ADDR_MASK);
            ept_free_pt(pt);
        }
    }
    ept_free_table(pd);
}

static void ept_free_pdpt(uint64_t *pdpt)
{
    if (pdpt == NULL) return;
    for (uint64_t i = 0; i < 512; i++) {
        if ((pdpt[i] & EPT_RWX) != 0) {
            uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[i] & EPT_ADDR_MASK);
            ept_free_pd(pd);
        }
    }
    ept_free_table(pdpt);
}

void ept_destroy(vmx_ept_t *ept)
{
    if (ept == NULL) return;

    if (ept->pml4 != NULL) {
        for (uint64_t i = 0; i < 512; i++) {
            if ((ept->pml4[i] & EPT_RWX) != 0) {
                uint64_t *pdpt = (uint64_t *)(uintptr_t)(ept->pml4[i] & EPT_ADDR_MASK);
                ept_free_pdpt(pdpt);
            }
        }
        ept_free_table(ept->pml4);
        ept->pml4 = NULL;
    }

    free(ept);
}
