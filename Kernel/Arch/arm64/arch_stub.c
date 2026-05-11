#include "interfaces/arch_ops.h"
#include "interfaces/mmu_ops.h"

static void arm64_noop(void)
{
}

static uint64_t arm64_zero(void)
{
    return 0;
}

static void *arm64_null_map(uint64_t phys_addr)
{
    (void)phys_addr;
    return 0;
}

static void arm64_restore(uint64_t flags)
{
    (void)flags;
}

static int arm64_unsupported(void)
{
    return -1;
}

static const arch_ops_t g_arm64_arch_ops = {
    .early_init = arm64_noop,
    .init_cpu_tables = arm64_noop,
    .enable_interrupts = arm64_noop,
    .disable_interrupts = arm64_noop,
    .irq_save_disable = arm64_zero,
    .irq_restore = arm64_restore,
    .enter_user_mode = 0,
    .virtualization_init = arm64_unsupported,
};

static const mmu_ops_t g_arm64_mmu_ops = {
    .init = arm64_noop,
    .map_mmio = arm64_null_map,
    .kernel_address_space = arm64_zero,
    .active_address_space = arm64_zero,
    .switch_address_space = 0,
    .create_address_space = arm64_zero,
    .destroy_address_space = 0,
    .map_user_page = 0,
    .map_user_range_alloc = 0,
    .unmap_range = 0,
    .virt_to_phys = 0,
};

const arch_ops_t *arch_ops_get(void)
{
    return &g_arm64_arch_ops;
}

const mmu_ops_t *mmu_ops_get(void)
{
    return &g_arm64_mmu_ops;
}
