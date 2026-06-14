#include "Syscall_Main.h"
#include "mmu/Paging_Main.h"
#include "Core/process/ProcessManager.h"
#include "Debug/printf/printf.h"

#include <stddef.h>
#include <stdint.h>

int64_t syscall_vm_mprotect(uint64_t addr, uint64_t len, uint64_t prot)
{
    enum { PROT_READ = 1u, PROT_WRITE = 2u, PROT_EXEC = 4u };
    if (len == 0 || (addr & (PAGE_SIZE - 1ULL)) != 0u ||
        (prot & ~(uint64_t)(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0u)
        return -22;

    uint64_t cr3 = process_get_current_cr3();
    if (cr3 == 0) return -14;
    uint64_t flags = 0u;
    if ((prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0u)
        flags |= PAGE_USER;
    if ((prot & PROT_WRITE) != 0u) flags |= PAGE_RW;
    if ((prot & PROT_EXEC) == 0u) flags |= PAGE_NX;
    return paging_protect_user_range(cr3, addr, len, flags) < 0 ? -14 : 0;
}

int64_t syscall_vm_munmap(uint64_t addr, uint64_t len)
{
    if (len == 0) {
        return 0;
    }

    int rc = process_user_munmap((void *)(uintptr_t)addr, len);
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
