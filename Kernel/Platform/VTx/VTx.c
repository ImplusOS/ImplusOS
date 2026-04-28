#include "VTx.h"
#include "EPT.h"
#include "../../Memory/Memory_Main.h"
#include "../../Paging/Paging_Main.h"
#include "../../Sync/Spinlock.h"
#include "../../Debbuger/Serial/Serial.h"
#include "../../Debbuger/printf/printf.h"
#include "../../GDT/GDT_Main.h"
#include "../../IDT/IDT_Main.h"
#include "../../KernelConfig.h"
#include "../../Memory/DMA_Memory.h"
#include <string.h>
#include <stddef.h>

static int      g_vtx_available = 0;
static vmx_vm_t g_vms[VMX_MAX_VMS];
static spinlock_t g_vtx_lock;
static vmx_vm_t *g_current_vm = NULL;

static uint8_t g_vmxon_region_buf[4096] __attribute__((aligned(4096)));

static uint8_t g_msr_bitmap[4096]    __attribute__((aligned(4096)));
static uint8_t g_io_bitmap_a[4096]   __attribute__((aligned(4096)));
static uint8_t g_io_bitmap_b[4096]   __attribute__((aligned(4096)));
static uint8_t g_vapic_page[4096]    __attribute__((aligned(4096)));

extern int vmx_run_guest(vmx_regs_t *guest_regs, int launched);

vmx_vm_t *vtx_get_current_vm(void) { return g_current_vm; }

uint64_t vtx_adjust_cr0(uint64_t cr0)
{
    uint64_t fixed0 = rdmsr(IA32_VMX_CR0_FIXED0);
    uint64_t fixed1 = rdmsr(IA32_VMX_CR0_FIXED1);
    uint64_t new_cr0 = (cr0 | fixed0) & fixed1;

    uint64_t proc_msr_val = rdmsr(IA32_VMX_PROCBASED_CTLS2);
    uint32_t proc2_allowed1 = (uint32_t)(proc_msr_val >> 32);
    int has_unrestricted = (proc2_allowed1 >> 7) & 1;

    if (has_unrestricted) {
        if (!(cr0 & CR0_PE)) new_cr0 &= ~CR0_PE;
        if (!(cr0 & CR0_PG)) new_cr0 &= ~CR0_PG;
    }
    return new_cr0;
}

uint64_t vtx_adjust_cr4(uint64_t cr4)
{
    uint64_t fixed0 = rdmsr(IA32_VMX_CR4_FIXED0);
    uint64_t fixed1 = rdmsr(IA32_VMX_CR4_FIXED1);
    cr4 |= fixed0;
    cr4 &= fixed1;
    return cr4;
}

static uint32_t vtx_adjust_controls(uint32_t desired, uint32_t msr_num)
{
    uint64_t msr_val = rdmsr(msr_num);
    uint32_t allowed0 = (uint32_t)(msr_val & 0xFFFFFFFF);
    uint32_t allowed1 = (uint32_t)(msr_val >> 32);
    desired |= allowed0;
    desired &= allowed1;
    return desired;
}

int vtx_init(void)
{
    spinlock_init(&g_vtx_lock);
    memset(g_vms, 0, sizeof(g_vms));

    uint32_t eax_out, ebx_out, ecx_out, edx_out;
    __asm__ volatile("cpuid"
                     : "=a"(eax_out), "=b"(ebx_out), "=c"(ecx_out), "=d"(edx_out)
                     : "a"(1), "c"(0));
    if (!(ecx_out & (1U << 5))) {
        return -1;
    }

    uint64_t feature_ctrl = rdmsr(IA32_FEATURE_CONTROL);
    if (feature_ctrl & FEATURE_CONTROL_LOCKED) {
        if (!(feature_ctrl & FEATURE_CONTROL_VMXON)) {
            return -1;
        }
    } else {
        wrmsr(IA32_FEATURE_CONTROL,
              feature_ctrl | FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMXON);
    }

    uint64_t cr4 = read_cr4();
    cr4 |= CR4_VMXE;
    write_cr4(cr4);

    uint64_t cr0 = read_cr0();
    write_cr0(vtx_adjust_cr0(cr0));

    memset(g_vmxon_region_buf, 0, 4096);

    uint64_t vmx_basic = rdmsr(IA32_VMX_BASIC);
    uint32_t revision_id = (uint32_t)(vmx_basic & 0x7FFFFFFF);
    *(uint32_t *)g_vmxon_region_buf = revision_id;

    uint64_t vmxon_phys = virt_to_phys(g_vmxon_region_buf);
    if (vmx_vmxon(&vmxon_phys) < 0) {
        return -1;
    }

    g_vtx_available = 1;
    return 0;
}

int vtx_is_available(void) { return g_vtx_available; }

static void vtx_setup_vmcs_host_state(void)
{
    vmx_vmwrite(VMCS_HOST_CR0, read_cr0());
    vmx_vmwrite(VMCS_HOST_CR3, read_cr3_vtx());
    vmx_vmwrite(VMCS_HOST_CR4, read_cr4());

    uint16_t cs, ss, ds, es, fs, gs, tr;
    __asm__ volatile("mov %%cs, %0" : "=rm"(cs));
    __asm__ volatile("mov %%ss, %0" : "=rm"(ss));
    __asm__ volatile("mov %%ds, %0" : "=rm"(ds));
    __asm__ volatile("mov %%es, %0" : "=rm"(es));
    __asm__ volatile("mov %%fs, %0" : "=rm"(fs));
    __asm__ volatile("mov %%gs, %0" : "=rm"(gs));
    __asm__ volatile("str %0"       : "=rm"(tr));

    vmx_vmwrite(VMCS_HOST_CS_SELECTOR, cs & 0xFFF8);
    vmx_vmwrite(VMCS_HOST_SS_SELECTOR, ss & 0xFFF8);
    vmx_vmwrite(VMCS_HOST_DS_SELECTOR, ds & 0xFFF8);
    vmx_vmwrite(VMCS_HOST_ES_SELECTOR, es & 0xFFF8);
    vmx_vmwrite(VMCS_HOST_FS_SELECTOR, fs & 0xFFF8);
    vmx_vmwrite(VMCS_HOST_GS_SELECTOR, gs & 0xFFF8);
    vmx_vmwrite(VMCS_HOST_TR_SELECTOR, tr & 0xFFF8);

    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr, idtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    __asm__ volatile("sidt %0" : "=m"(idtr));

    vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtr.base);
    vmx_vmwrite(VMCS_HOST_IDTR_BASE, idtr.base);

    uint64_t *gdt_entries = (uint64_t *)(uintptr_t)gdtr.base;
    uint64_t tss_desc_lo = gdt_entries[tr >> 3];
    uint64_t tss_desc_hi = gdt_entries[(tr >> 3) + 1];
    uint64_t tr_base = ((tss_desc_lo >> 16) & 0xFFFF) |
                       (((tss_desc_lo >> 32) & 0xFF) << 16) |
                       (((tss_desc_lo >> 56) & 0xFF) << 24) |
                       ((tss_desc_hi & 0xFFFFFFFF) << 32);
    vmx_vmwrite(VMCS_HOST_TR_BASE, tr_base);

    vmx_vmwrite(VMCS_HOST_FS_BASE, 0);
    vmx_vmwrite(VMCS_HOST_GS_BASE, rdmsr(0xC0000101));

    vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, 0);
    vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, 0);

    vmx_vmwrite(VMCS_HOST_IA32_EFER, rdmsr(0xC0000080));

    extern void vmx_vmexit_entry(void);
    vmx_vmwrite(VMCS_HOST_RIP, (uint64_t)(uintptr_t)vmx_vmexit_entry);
}

static void vtx_setup_vmcs_guest_state(vmx_vm_t *vm)
{
    vmx_regs_t *r = &vm->guest_regs;

    vmx_vmwrite(VMCS_GUEST_CR0, vtx_adjust_cr0(r->cr0));
    vmx_vmwrite(VMCS_GUEST_CR3, r->cr3);
    vmx_vmwrite(VMCS_GUEST_CR4, vtx_adjust_cr4(r->cr4));

    vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    uint32_t seg_access = 0x93;
    uint32_t cs_access  = 0x93;
    uint32_t tr_access  = 0x8B;
    uint32_t unusable    = 0x10000;

    vmx_vmwrite(VMCS_GUEST_CS_SELECTOR, r->cs);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, r->cs_base ? r->cs_base : (r->cs << 4));
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS_RIGHTS, cs_access);

    vmx_vmwrite(VMCS_GUEST_DS_SELECTOR, r->ds);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, r->ds << 4);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS_RIGHTS, seg_access);

    vmx_vmwrite(VMCS_GUEST_ES_SELECTOR, r->es);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, r->es << 4);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS_RIGHTS, seg_access);

    vmx_vmwrite(VMCS_GUEST_FS_SELECTOR, r->fs);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, r->fs << 4);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS_RIGHTS, seg_access);

    vmx_vmwrite(VMCS_GUEST_GS_SELECTOR, r->gs);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, r->gs << 4);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS_RIGHTS, seg_access);

    vmx_vmwrite(VMCS_GUEST_SS_SELECTOR, r->ss);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, r->ss << 4);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS_RIGHTS, seg_access);

    vmx_vmwrite(VMCS_GUEST_TR_SELECTOR, r->tr);
    vmx_vmwrite(VMCS_GUEST_TR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_TR_ACCESS_RIGHTS, tr_access);

    vmx_vmwrite(VMCS_GUEST_LDTR_SELECTOR, r->ldtr);
    vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS_RIGHTS, unusable);

    vmx_vmwrite(VMCS_GUEST_GDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_IDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, 0xFFFF);

    vmx_vmwrite(VMCS_GUEST_RSP, r->rsp);
    vmx_vmwrite(VMCS_GUEST_RIP, r->rip);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, r->rflags | 0x2);

    vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);
    vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, 0);

    vmx_vmwrite(VMCS_GUEST_IA32_EFER, 0);

    vmx_vmwrite(VMCS_LINK_POINTER, 0xFFFFFFFFFFFFFFFFULL);
}

static void vtx_setup_vmcs_controls(vmx_vm_t *vm)
{
    uint64_t vmx_basic = rdmsr(IA32_VMX_BASIC);
    int use_true_msrs = (vmx_basic >> 55) & 1;

    uint32_t pin_msr  = use_true_msrs ? IA32_VMX_TRUE_PINBASED_CTLS  : IA32_VMX_PINBASED_CTLS;
    uint32_t proc_msr = use_true_msrs ? IA32_VMX_TRUE_PROCBASED_CTLS : IA32_VMX_PROCBASED_CTLS;
    uint32_t exit_msr = use_true_msrs ? IA32_VMX_TRUE_EXIT_CTLS      : IA32_VMX_EXIT_CTLS;
    uint32_t ent_msr  = use_true_msrs ? IA32_VMX_TRUE_ENTRY_CTLS     : IA32_VMX_ENTRY_CTLS;

    uint32_t pin_based = PIN_BASED_EXT_INT_EXIT | PIN_BASED_NMI_EXIT;
    pin_based = vtx_adjust_controls(pin_based, pin_msr);
    vmx_vmwrite(VMCS_PIN_BASED_CONTROLS, pin_based);

    uint64_t proc_msr_val = rdmsr(proc_msr);
    uint32_t proc_allowed1 = (uint32_t)(proc_msr_val >> 32);
    int has_secondary = (proc_allowed1 >> 31) & 1;

    int has_ept = 0, has_unrestricted = 0;
    if (has_secondary) {
        uint64_t proc2_msr_val = rdmsr(IA32_VMX_PROCBASED_CTLS2);
        uint32_t proc2_allowed1 = (uint32_t)(proc2_msr_val >> 32);
        has_ept = (proc2_allowed1 >> 1) & 1;
        has_unrestricted = (proc2_allowed1 >> 7) & 1;
    }

    uint32_t proc_based = PROC_BASED_HLT_EXIT | PROC_BASED_UNCOND_IO_EXIT;
    if (has_secondary) {
        proc_based |= PROC_BASED_ACTIVATE_SECONDARY;
    }
    proc_based = vtx_adjust_controls(proc_based, proc_msr);
    vmx_vmwrite(VMCS_PROC_BASED_CONTROLS, proc_based);

    if (proc_based & (1U << 28)) {
        memset(g_msr_bitmap, 0x00, 4096);

        g_msr_bitmap[0x400 + 0x10] |= (1U << 0);
        g_msr_bitmap[0xC00 + 0x10] |= (1U << 0);

        g_msr_bitmap[0x000 + (0x1B >> 3)] |= (1U << (0x1B & 7));
        g_msr_bitmap[0x800 + (0x1B >> 3)] |= (1U << (0x1B & 7));

        g_msr_bitmap[0x000 + (0xFE >> 3)] |= (1U << (0xFE & 7));
        g_msr_bitmap[0x000 + (0x2FF >> 3)] |= (1U << (0x2FF & 7));
        g_msr_bitmap[0x800 + (0x2FF >> 3)] |= (1U << (0x2FF & 7));
        for (uint32_t m = 0x250; m <= 0x259; m++) {
            g_msr_bitmap[0x000 + (m >> 3)] |= (1U << (m & 7));
            g_msr_bitmap[0x800 + (m >> 3)] |= (1U << (m & 7));
        }
        for (uint32_t m = 0x268; m <= 0x26F; m++) {
            g_msr_bitmap[0x000 + (m >> 3)] |= (1U << (m & 7));
            g_msr_bitmap[0x800 + (m >> 3)] |= (1U << (m & 7));
        }
        for (uint32_t m = 0x200; m <= 0x20F; m++) {
            g_msr_bitmap[0x000 + (m >> 3)] |= (1U << (m & 7));
            g_msr_bitmap[0x800 + (m >> 3)] |= (1U << (m & 7));
        }
        g_msr_bitmap[0x000 + (0x277 >> 3)] |= (1U << (0x277 & 7));
        g_msr_bitmap[0x800 + (0x277 >> 3)] |= (1U << (0x277 & 7));

        vmx_vmwrite(0x2004, virt_to_phys(g_msr_bitmap));
    }

    if (proc_based & (1U << 25)) {
        memset(g_io_bitmap_a, 0xFF, 4096);
        memset(g_io_bitmap_b, 0xFF, 4096);
        vmx_vmwrite(0x2000, virt_to_phys(g_io_bitmap_a));
        vmx_vmwrite(0x2002, virt_to_phys(g_io_bitmap_b));
    }

    if (proc_based & (1U << 21)) {
        memset(g_vapic_page, 0, 4096);
        vmx_vmwrite(0x2012, virt_to_phys(g_vapic_page));
        vmx_vmwrite(0x4824, 0);
    }

    if (has_secondary) {
        uint32_t proc_based2 = 0;
        if (has_ept)          proc_based2 |= PROC2_BASED_EPT;
        if (has_unrestricted) proc_based2 |= PROC2_BASED_UNRESTRICTED;
        proc_based2 = vtx_adjust_controls(proc_based2, IA32_VMX_PROCBASED_CTLS2);
        vmx_vmwrite(VMCS_PROC_BASED_CONTROLS2, proc_based2);
    }

    uint32_t exit_ctrls = EXIT_CTRL_HOST_ADDR_SPACE_SIZE |
                          EXIT_CTRL_ACK_INT_ON_EXIT;
    uint64_t exit_msr_val = rdmsr(exit_msr);
    uint32_t exit_allowed1 = (uint32_t)(exit_msr_val >> 32);
    if ((exit_allowed1 >> 20) & 1) exit_ctrls |= (1U << 20);
    if ((exit_allowed1 >> 21) & 1) exit_ctrls |= (1U << 21);
    exit_ctrls = vtx_adjust_controls(exit_ctrls, exit_msr);
    vmx_vmwrite(VMCS_EXIT_CONTROLS, exit_ctrls);

    uint32_t entry_ctrls = 0;
    uint64_t ent_msr_val = rdmsr(ent_msr);
    uint32_t ent_allowed1 = (uint32_t)(ent_msr_val >> 32);
    if ((ent_allowed1 >> 15) & 1) entry_ctrls |= (1U << 15);
    entry_ctrls = vtx_adjust_controls(entry_ctrls, ent_msr);
    vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctrls);

    vmx_vmwrite(0x4016, 0);

    vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);

    if (has_ept && vm->ept != NULL) {
        vmx_vmwrite(VMCS_EPT_POINTER, vm->ept->eptp);
    }

    uint64_t cr0_fixed0 = rdmsr(IA32_VMX_CR0_FIXED0);
    uint64_t cr0_fixed1 = rdmsr(IA32_VMX_CR0_FIXED1);
    uint64_t cr0_mask = cr0_fixed0 | ~cr0_fixed1;

    uint64_t cr4_fixed0 = rdmsr(IA32_VMX_CR4_FIXED0);
    uint64_t cr4_fixed1 = rdmsr(IA32_VMX_CR4_FIXED1);
    uint64_t cr4_mask = cr4_fixed0 | ~cr4_fixed1;

    if (has_unrestricted) {
        cr0_mask &= ~(CR0_PE | CR0_PG);
    }

    vmx_vmwrite(0x6000, cr0_mask);
    vmx_vmwrite(0x6002, cr4_mask);
    vmx_vmwrite(0x6004, 0);
    vmx_vmwrite(0x6006, 0);

    vmx_vmwrite(0x4008, 0);
    vmx_vmwrite(0x400A, 0);
    vmx_vmwrite(0x400E, 0);
}

int32_t vtx_vm_create(void)
{
    if (!g_vtx_available) return -1;

    spinlock_lock(&g_vtx_lock);
    int32_t vm_id = -1;
    for (int i = 0; i < VMX_MAX_VMS; i++) {
        if (!g_vms[i].active) { vm_id = i; break; }
    }
    if (vm_id < 0) { spinlock_unlock(&g_vtx_lock); return -1; }

    vmx_vm_t *vm = &g_vms[vm_id];
    memset(vm, 0, sizeof(*vm));
    vm->active = 1;
    spinlock_unlock(&g_vtx_lock);

    vm->vmcs_region = alloc_page();
    if (!vm->vmcs_region) { vm->active = 0; return -1; }
    memset(vm->vmcs_region, 0, 4096);

    uint64_t vmx_basic = rdmsr(IA32_VMX_BASIC);
    *(uint32_t *)vm->vmcs_region = (uint32_t)(vmx_basic & 0x7FFFFFFF);
    vm->vmcs_phys = virt_to_phys(vm->vmcs_region);

    vm->ept = ept_create();
    if (!vm->ept) {
        free_page(vm->vmcs_region);
        vm->active = 0;
        return -1;
    }

    vm->guest_regs.cr0 = CR0_PE | CR0_NE;
    vm->guest_regs.cr4 = 0;
    vm->guest_regs.rflags = 0x2;
    vm->guest_regs.rip = 0xFFF0;
    vm->guest_regs.rsp = 0;
    vm->guest_regs.cs = 0xF000;
    vm->guest_regs.ds = 0;
    vm->guest_regs.es = 0;
    vm->guest_regs.fs = 0;
    vm->guest_regs.gs = 0;
    vm->guest_regs.ss = 0;

    return vm_id;
}

void vtx_vm_destroy(int32_t vm_id)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS) return;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return;

    if (vm->vmcs_region) {
        vmx_vmclear(&vm->vmcs_phys);
        free_page(vm->vmcs_region);
    }
    if (vm->ept) ept_destroy(vm->ept);
    memset(vm, 0, sizeof(*vm));
}

int vtx_vm_set_regs(int32_t vm_id, const vmx_regs_t *regs)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS || !regs) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return -1;
    memcpy(&vm->guest_regs, regs, sizeof(vmx_regs_t));
    return 0;
}

int vtx_vm_get_regs(int32_t vm_id, vmx_regs_t *regs)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS || !regs) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return -1;
    memcpy(regs, &vm->guest_regs, sizeof(vmx_regs_t));
    return 0;
}

static uint64_t vtx_get_phys_from_virt(uint64_t cr3, uint64_t virt)
{
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & ~0xFFFULL);
    uint16_t i4 = (virt >> 39) & 0x1FF;
    uint16_t i3 = (virt >> 30) & 0x1FF;
    uint16_t i2 = (virt >> 21) & 0x1FF;
    uint16_t i1 = (virt >> 12) & 0x1FF;

    if (!(pml4[i4] & 1)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i4] & ~0xFFFULL);
    
    if (!(pdpt[i3] & 1)) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[i3] & ~0xFFFULL);

    if (!(pd[i2] & 1)) return 0;
    if (pd[i2] & 0x80) {
        return (pd[i2] & ~0x1FFFFFULL) + (virt & 0x1FFFFF);
    }
    
    uint64_t *pt = (uint64_t *)(uintptr_t)(pd[i2] & ~0xFFFULL);
    if (!(pt[i1] & 1)) return 0;
    
    return (pt[i1] & ~0xFFFULL) + (virt & 0xFFF);
}

int vtx_vm_map_memory(int32_t vm_id, const vmx_memory_region_t *region)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS || !region) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active || !vm->ept) return -1;

    uint64_t cr3 = read_cr3_vtx();
    uint64_t offset = 0;
    
    while (offset < region->size) {
        uint64_t host_virt = region->host_virt_addr + offset;
        uint64_t host_phys = vtx_get_phys_from_virt(cr3, host_virt);
        
        if (host_phys == 0) {
            return -1;
        }

        int rc = ept_map_page(vm->ept,
                              region->guest_phys_addr + offset,
                              host_phys,
                              EPT_RWX | EPTP_MEMTYPE_WB);
        if (rc < 0) return rc;
        
        offset += 4096;
    }

    if (vm->mem_region_count < VMX_MAX_MEM_REGIONS) {
        vm->mem_regions[vm->mem_region_count] = *region;
        vm->mem_region_count++;
    }

    vmx_invept(2, 0);

    return 0;
}

int vtx_vm_set_framebuffer(int32_t vm_id, const vmx_framebuffer_t *fb)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS || !fb) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return -1;
    vm->guest_fb = *fb;
    vm->guest_fb_valid = 1;
    return 0;
}

int vtx_vm_get_exit_info(int32_t vm_id, vmx_exit_info_t *info)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS || !info) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return -1;
    memcpy(info, &vm->last_exit, sizeof(vmx_exit_info_t));
    return 0;
}

int vtx_vm_set_io_response(int32_t vm_id, uint32_t data)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return -1;
    vm->guest_regs.rax = (vm->guest_regs.rax & ~0xFFFFFFFFULL) | data;
    return 0;
}

int vtx_vm_run(int32_t vm_id)
{
    if (vm_id < 0 || vm_id >= VMX_MAX_VMS) return -1;
    vmx_vm_t *vm = &g_vms[vm_id];
    if (!vm->active) return -1;

    if (!vm->launched) {
        if (vmx_vmclear(&vm->vmcs_phys) < 0) {
            return -1;
        }
    }
    if (vmx_vmptrld(&vm->vmcs_phys) < 0) {
        return -1;
    }

    if (!vm->launched) {
        vtx_setup_vmcs_host_state();
        vtx_setup_vmcs_controls(vm);
        vtx_setup_vmcs_guest_state(vm);
    }

    g_current_vm = vm;

    int rc = vmx_run_guest(&vm->guest_regs, vm->launched ? 1 : 0);

    if (rc < 0) {
        uint64_t vm_err = 0;
        vmx_vmread(0x4400, &vm_err);

        g_current_vm = NULL;
        return -1;
    }

    uint64_t raw_exit_reason = 0;
    vmx_vmread(0x4402, &raw_exit_reason);
    if (raw_exit_reason & 0x80000000ULL) {
        g_current_vm = NULL;
        return -1;
    }

    if (!vm->launched) {
        vm->launched = 1;
    }

    g_current_vm = NULL;
    return 0;
}