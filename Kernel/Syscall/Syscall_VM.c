#include "Syscall_Main.h"
#include "../Paging/Paging_Main.h"
#include "../ProcessManager/ProcessManager.h"
#include "../Debbuger/printf/printf.h"

#include <stddef.h>
#include <stdint.h>

int64_t syscall_vm_mprotect(uint64_t addr, uint64_t len, uint64_t prot)
{
    if (len == 0) {
        return 0;
    }

    uint64_t page_mask = ~(PAGE_SIZE - 1ULL);
    uint64_t start = addr & page_mask;
    uint64_t end   = (addr + len + PAGE_SIZE - 1ULL) & page_mask;
    (void)end;

    uint64_t cr3 = process_get_current_cr3();
    if (cr3 == 0) {
        return -14;
    }

    (void)prot;

    return 0;
}

int64_t syscall_vm_munmap(uint64_t addr, uint64_t len)
{
    if (len == 0) {
        return 0;
    }

    uint64_t page_mask = ~(PAGE_SIZE - 1ULL);
    uint64_t start = addr & page_mask;
    uint64_t end   = (addr + len + PAGE_SIZE - 1ULL) & page_mask;
    uint64_t size  = end - start;

    uint64_t cr3 = process_get_current_cr3();
    if (cr3 == 0) {
        return -14;
    }

    int rc = paging_unmap_range(cr3, start, size);
    return (rc < 0) ? -14 : 0;
}

int64_t syscall_vm_mremap(uint64_t old_addr, uint64_t old_size,
                          uint64_t new_size, uint64_t flags)
{
    (void)old_addr;
    (void)old_size;
    (void)new_size;
    (void)flags;
    return -38;
}
