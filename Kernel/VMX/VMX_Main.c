#include "VMX.h"
#include "../Memory/Memory_Main.h"
#include "../Debbuger/Serial/Serial.h"
#include "../Sync/Spinlock.h"
#include "../KernelConfig.h"
#include "../GDT/GDT_Main.h"
#include <string.h>
#include <stddef.h>

/* ── MTRR variable range storage ───────────────────────────── */
#define MTRR_VAR_COUNT 8
static uint64_t g_mtrr_var_base[MTRR_VAR_COUNT];
static uint64_t g_mtrr_var_mask[MTRR_VAR_COUNT];
static uint64_t g_mtrr_def_type = 6; /* WB */
static uint64_t g_guest_pat = 0x0007040600070406ULL;

/* ── Globals ───────────────────────────────────────────────── */
static uint8_t  g_vmx_enabled = 0;
static void    *g_vmxon_region = NULL;
static uint32_t g_vmcs_revision = 0;
static spinlock_t g_vmx_lock;

/* Host stack for VM exits (per-vCPU, 16KB) */
static uint8_t g_vmx_host_stack[16384] __attribute__((aligned(16)));

/* ── Forward declarations ──────────────────────────────────── */
extern void vmx_vmexit_handler(void);

static uint32_t vmx_adjust_controls(uint32_t desired, uint32_t msr);
static void     vmx_setup_vmcs_host(vmx_vcpu_t *vcpu);
static void     vmx_setup_vmcs_guest(vmx_vcpu_t *vcpu);
static void     vmx_setup_vmcs_controls(vmx_vcpu_t *vcpu);
static int      vmx_handle_exit(vmx_vcpu_t *vcpu);
static void     vmx_handle_cpuid(vmx_vcpu_t *vcpu);
static int      vmx_handle_io(vmx_vcpu_t *vcpu);
static int      vmx_handle_msr_read(vmx_vcpu_t *vcpu);
static int      vmx_handle_msr_write(vmx_vcpu_t *vcpu);
static int      vmx_handle_ept_violation(vmx_vcpu_t *vcpu);
static int      vmx_handle_cr_access(vmx_vcpu_t *vcpu);

/* ── VMX init / shutdown ───────────────────────────────────── */

int vmx_is_supported(void)
{
    uint32_t ecx;
    __asm__ volatile("cpuid" : "=c"(ecx) : "a"(1) : "ebx", "edx");
    return (ecx >> 5) & 1;
}

int vmx_init(void)
{
    spinlock_init(&g_vmx_lock);

    if (!vmx_is_supported()) {
        serial_write_string("[VMX] VT-x not supported\n");
        return -1;
    }

    /* Check/set IA32_FEATURE_CONTROL */
    uint64_t feat = vmx_rdmsr(IA32_FEATURE_CONTROL);
    if (!(feat & FEATURE_CONTROL_LOCKED)) {
        feat |= FEATURE_CONTROL_LOCKED | FEATURE_CONTROL_VMXON_OUTSIDE;
        vmx_wrmsr(IA32_FEATURE_CONTROL, feat);
    } else if (!(feat & FEATURE_CONTROL_VMXON_OUTSIDE)) {
        serial_write_string("[VMX] VMXON disabled by BIOS\n");
        return -1;
    }

    /* Read VMCS revision from IA32_VMX_BASIC */
    uint64_t vmx_basic = vmx_rdmsr(IA32_VMX_BASIC);
    g_vmcs_revision = (uint32_t)(vmx_basic & 0x7FFFFFFFULL);

    /* Set CR4.VMXE */
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_VMXE;
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    /* Adjust CR0 for VMX fixed bits */
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= vmx_rdmsr(IA32_VMX_CR0_FIXED0);
    cr0 &= vmx_rdmsr(IA32_VMX_CR0_FIXED1);
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    /* Adjust CR4 for VMX fixed bits */
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= vmx_rdmsr(IA32_VMX_CR4_FIXED0);
    cr4 &= vmx_rdmsr(IA32_VMX_CR4_FIXED1);
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    /* Allocate VMXON region */
    g_vmxon_region = alloc_page();
    if (!g_vmxon_region) {
        serial_write_string("[VMX] Failed to allocate VMXON region\n");
        return -1;
    }
    memset(g_vmxon_region, 0, 4096);
    *(uint32_t *)g_vmxon_region = g_vmcs_revision;

    /* Execute VMXON */
    uint64_t vmxon_phys = (uint64_t)(uintptr_t)g_vmxon_region;
    uint8_t err;
    __asm__ volatile(
        "vmxon %1\n\t"
        "setna %0"
        : "=rm"(err)
        : "m"(vmxon_phys)
        : "cc", "memory"
    );
    if (err) {
        serial_write_string("[VMX] VMXON failed\n");
        free_page(g_vmxon_region);
        g_vmxon_region = NULL;
        return -1;
    }

    g_vmx_enabled = 1;
    serial_write_string("[VMX] VMXON success, revision=");
    serial_write_uint32(g_vmcs_revision);
    serial_write_string("\n");
    return 0;
}

void vmx_shutdown(void)
{
    if (!g_vmx_enabled) return;
    __asm__ volatile("vmxoff" ::: "cc");
    if (g_vmxon_region) {
        free_page(g_vmxon_region);
        g_vmxon_region = NULL;
    }
    g_vmx_enabled = 0;
    serial_write_string("[VMX] VMXOFF done\n");
}

/* ── Control adjustment ────────────────────────────────────── */

static uint32_t vmx_adjust_controls(uint32_t desired, uint32_t msr)
{
    uint64_t val = vmx_rdmsr(msr);
    uint32_t allowed0 = (uint32_t)val;         /* must-be-1 */
    uint32_t allowed1 = (uint32_t)(val >> 32);  /* may-be-1 */
    desired |= allowed0;
    desired &= allowed1;
    return desired;
}

/* ── vCPU create / destroy ─────────────────────────────────── */

int vmx_vcpu_create(vmx_vcpu_t *vcpu)
{
    if (!vcpu || !g_vmx_enabled) return -1;

    memset(vcpu, 0, sizeof(*vcpu));

    /* Allocate VMCS */
    vcpu->vmcs_region = alloc_page();
    if (!vcpu->vmcs_region) {
        serial_write_string("[VMX] Failed to allocate VMCS\n");
        return -1;
    }
    memset(vcpu->vmcs_region, 0, 4096);
    *(uint32_t *)vcpu->vmcs_region = g_vmcs_revision;

    /* Allocate kvm_run shared page */
    vcpu->run = (kvm_run_t *)alloc_page();
    if (!vcpu->run) {
        free_page(vcpu->vmcs_region);
        return -1;
    }
    memset(vcpu->run, 0, 4096);

    /* Create EPT */
    if (ept_create(vcpu) < 0) {
        free_page(vcpu->vmcs_region);
        free_page(vcpu->run);
        return -1;
    }

    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;

    /* VMCLEAR */
    __asm__ volatile("vmclear %0" :: "m"(vmcs_phys) : "cc", "memory");

    /* VMPTRLD */
    uint8_t err;
    __asm__ volatile(
        "vmptrld %1\n\t"
        "setna %0"
        : "=rm"(err)
        : "m"(vmcs_phys)
        : "cc", "memory"
    );
    if (err) {
        serial_write_string("[VMX] VMPTRLD failed\n");
        ept_destroy(vcpu);
        free_page(vcpu->vmcs_region);
        free_page(vcpu->run);
        return -1;
    }

    vmx_setup_vmcs_controls(vcpu);
    vmx_setup_vmcs_host(vcpu);
    vmx_setup_vmcs_guest(vcpu);

    vcpu->active = 1;
    vcpu->launched = 0;

    serial_write_string("[VMX] vCPU created\n");
    return 0;
}

void vmx_vcpu_destroy(vmx_vcpu_t *vcpu)
{
    if (!vcpu || !vcpu->active) return;

    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;
    __asm__ volatile("vmclear %0" :: "m"(vmcs_phys) : "cc", "memory");

    ept_destroy(vcpu);
    if (vcpu->vmcs_region) free_page(vcpu->vmcs_region);
    if (vcpu->run) free_page(vcpu->run);

    vcpu->active = 0;
    serial_write_string("[VMX] vCPU destroyed\n");
}

/* ── VMCS setup: controls ──────────────────────────────────── */

static void vmx_setup_vmcs_controls(vmx_vcpu_t *vcpu)
{
    /* Check if TRUE controls MSRs are available (bit 55 of VMX_BASIC) */
    uint64_t vmx_basic = vmx_rdmsr(IA32_VMX_BASIC);
    int use_true = (vmx_basic >> 55) & 1;

    /* Pin-based: request only mandatory bits.
     * Nested KVM is sensitive to delivery-event exits very early in boot,
     * and OVMF reset execution does not require intercepting interrupts/NMIs. */
    uint32_t pin_msr = use_true ? IA32_VMX_TRUE_PINBASED_CTLS : IA32_VMX_PINBASED_CTLS;
    uint32_t pin = vmx_adjust_controls(0, pin_msr);
    vmx_vmwrite(VMCS_PIN_BASED_CONTROLS, pin);

    /* Primary proc-based */
    uint32_t proc1_msr = use_true ? IA32_VMX_TRUE_PROCBASED_CTLS : IA32_VMX_PROCBASED_CTLS;
    uint32_t proc1 = vmx_adjust_controls(
        CPU_BASED_HLT_EXITING |
        CPU_BASED_UNCOND_IO_EXITING |
        CPU_BASED_USE_MSR_BITMAPS |
        CPU_BASED_ACTIVATE_SECONDARY,
        proc1_msr
    );
    vmx_vmwrite(VMCS_CPU_BASED_CONTROLS, proc1);

    /* Secondary proc-based: EPT + Unrestricted Guest */
    uint32_t proc2 = vmx_adjust_controls(
        SECONDARY_EXEC_ENABLE_EPT |
        SECONDARY_EXEC_UNRESTRICTED |
        SECONDARY_EXEC_RDTSCP,
        IA32_VMX_PROCBASED_CTLS2
    );
    vmx_vmwrite(VMCS_SECONDARY_CPU_CONTROLS, proc2);

    /* Exit controls: keep the host in 64-bit mode and preserve EFER.
     * Avoid ACK_INTR_ON_EXIT because we are not consuming injected IRQs yet
     * and nested KVM can trip over delivery-event exits during reset. */
    uint32_t exit_msr = use_true ? IA32_VMX_TRUE_EXIT_CTLS : IA32_VMX_EXIT_CTLS;
    uint32_t exit_ctl = vmx_adjust_controls(
        VM_EXIT_HOST_ADDR_SPACE_SIZE |
        VM_EXIT_SAVE_IA32_EFER |
        VM_EXIT_LOAD_IA32_EFER,
        exit_msr
    );
    vmx_vmwrite(VMCS_EXIT_CONTROLS, exit_ctl);

    /* Entry controls: load EFER (+ load PAT if forced by hardware) */
    uint32_t entry_msr = use_true ? IA32_VMX_TRUE_ENTRY_CTLS : IA32_VMX_ENTRY_CTLS;
    uint32_t entry_ctl = vmx_adjust_controls(
        VM_ENTRY_LOAD_IA32_EFER,
        entry_msr
    );
    vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctl);

    serial_write_string("[VMX] Controls: pin=0x");
    serial_write_uint32(pin);
    serial_write_string(" proc1=0x");
    serial_write_uint32(proc1);
    serial_write_string(" proc2=0x");
    serial_write_uint32(proc2);
    serial_write_string("\n");
    serial_write_string("[VMX] Controls: exit=0x");
    serial_write_uint32(exit_ctl);
    serial_write_string(" entry=0x");
    serial_write_uint32(entry_ctl);
    serial_write_string(" use_true=");
    serial_write_uint32((uint32_t)use_true);
    serial_write_string("\n");

    /* Exception bitmap: intercept nothing */
    vmx_vmwrite(VMCS_EXCEPTION_BITMAP, 0);

    /* MSR bitmap: allow all (zero = no exit) */
    void *msr_bitmap = alloc_page();
    if (msr_bitmap) {
        memset(msr_bitmap, 0, 4096);
        vmx_vmwrite(VMCS_MSR_BITMAP, (uint64_t)(uintptr_t)msr_bitmap);
    }

    vmx_vmwrite(VMCS_EXIT_MSR_STORE_COUNT, 0);
    vmx_vmwrite(VMCS_EXIT_MSR_LOAD_COUNT, 0);
    vmx_vmwrite(VMCS_ENTRY_MSR_LOAD_COUNT, 0);
    vmx_vmwrite(VMCS_CR3_TARGET_COUNT, 0);
    vmx_vmwrite(VMCS_PF_ERROR_CODE_MASK, 0);
    vmx_vmwrite(VMCS_PF_ERROR_CODE_MATCH, 0);

    /* EPT pointer */
    uint64_t eptp = vcpu->ept_root_hpa | EPTP_WB | EPTP_PAGE_WALK_4;
    vmx_vmwrite(VMCS_EPTP, eptp);

    /* CR0/CR4 guest/host masks:
     * CR0: no intercept (unrestricted guest handles mode transitions)
     * CR4: trap VMXE bit — hardware requires VMXE=1 in guest CR4,
     *       but the guest shouldn't see it. Use read shadow to hide it. */
    vmx_vmwrite(VMCS_CR0_GUEST_HOST_MASK, 0);
    vmx_vmwrite(VMCS_CR0_READ_SHADOW, 0);
    vmx_vmwrite(VMCS_CR4_GUEST_HOST_MASK, CR4_VMXE);
    vmx_vmwrite(VMCS_CR4_READ_SHADOW, 0);
}

/* ── VMCS setup: host state ────────────────────────────────── */

static void vmx_setup_vmcs_host(vmx_vcpu_t *vcpu)
{
    uint64_t cr0, cr3, cr4;
    struct GDTR gdtr;
    struct GDTR idtr;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

    vmx_vmwrite(VMCS_HOST_CR0, cr0);
    vmx_vmwrite(VMCS_HOST_CR3, cr3);
    /* Host CR4.VMXE must be 1 in VMCS host state (which it is in cr4) */
    vmx_vmwrite(VMCS_HOST_CR4, cr4);

    /* Host selectors (RPL must be 0) */
    vmx_vmwrite(VMCS_HOST_CS_SEL, GDT_KERNEL_CODE);
    
    /* For 64-bit host, SS, DS, ES, FS, GS selectors are not checked and can be 0 */
    vmx_vmwrite(VMCS_HOST_SS_SEL, 0);
    vmx_vmwrite(VMCS_HOST_DS_SEL, 0);
    vmx_vmwrite(VMCS_HOST_ES_SEL, 0);
    vmx_vmwrite(VMCS_HOST_FS_SEL, 0);
    vmx_vmwrite(VMCS_HOST_GS_SEL, 0);

    uint16_t tr_sel;
    __asm__ volatile("str %0" : "=r"(tr_sel));
    vmx_vmwrite(VMCS_HOST_TR_SEL, tr_sel & ~7); /* ensure RPL=0 */

    /* GDTR/IDTR */
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    __asm__ volatile("sidt %0" : "=m"(idtr));
    vmx_vmwrite(VMCS_HOST_GDTR_BASE, gdtr.base);
    vmx_vmwrite(VMCS_HOST_IDTR_BASE, idtr.base);

    /* TR base: read from GDT */
    uint64_t *gdt_entries = (uint64_t *)gdtr.base;
    uint32_t tr_idx = (tr_sel >> 3);
    uint64_t tss_lo = gdt_entries[tr_idx];
    uint64_t tss_hi = gdt_entries[tr_idx + 1];
    uint64_t tr_base = ((tss_lo >> 16) & 0xFFFF) |
                       (((tss_lo >> 32) & 0xFF) << 16) |
                       (((tss_lo >> 56) & 0xFF) << 24) |
                       ((tss_hi & 0xFFFFFFFF) << 32);
    vmx_vmwrite(VMCS_HOST_TR_BASE, tr_base);

    /* FS/GS base */
    vmx_vmwrite(VMCS_HOST_FS_BASE, vmx_rdmsr(IA32_FS_BASE_MSR));
    vmx_vmwrite(VMCS_HOST_GS_BASE, vmx_rdmsr(IA32_GS_BASE_MSR));

    /* Host IA32_EFER */
    vmx_vmwrite(VMCS_HOST_IA32_EFER, vmx_rdmsr(IA32_EFER_MSR));

    /* SYSENTER (required by VMCS) */
    vmx_vmwrite(VMCS_HOST_SYSENTER_CS, vmx_rdmsr(IA32_SYSENTER_CS));
    vmx_vmwrite(VMCS_HOST_SYSENTER_ESP, vmx_rdmsr(IA32_SYSENTER_ESP));
    vmx_vmwrite(VMCS_HOST_SYSENTER_EIP, vmx_rdmsr(IA32_SYSENTER_EIP));

    /* Host stack (per-vCPU) */
    uint64_t host_rsp = (uint64_t)(uintptr_t)(g_vmx_host_stack + sizeof(g_vmx_host_stack));
    vmx_vmwrite(VMCS_HOST_RSP, host_rsp);

    /* Host RIP: VM exit landing point */
    vmx_vmwrite(VMCS_HOST_RIP, (uint64_t)(uintptr_t)vmx_vmexit_handler);
}

/* ── VMCS setup: guest state (Real Mode for OVMF) ─────────── */

static void vmx_setup_vmcs_guest(vmx_vcpu_t *vcpu)
{
    (void)vcpu;
    /* Guest CR0: real mode defaults */
    uint64_t guest_cr0 = CR0_ET | CR0_NE;
    guest_cr0 |= vmx_rdmsr(IA32_VMX_CR0_FIXED0);
    /* Clear PE and PG (unrestricted guest allows this) */
    uint64_t cr0_fixed1 = vmx_rdmsr(IA32_VMX_CR0_FIXED1);
    guest_cr0 &= cr0_fixed1;
    /* With unrestricted guest, PE and PG can be 0 even if fixed0 says 1 */
    guest_cr0 &= ~(CR0_PE | CR0_PG);

    vmx_vmwrite(VMCS_GUEST_CR0, guest_cr0);
    vmx_vmwrite(VMCS_GUEST_CR3, 0);
    /* Keep VMXE set in the actual guest CR4 value and hide it with the
     * CR4 read shadow. Some CPUs reject VM-entry when the VMCS guest CR4
     * clears VMXE even though the guest-visible value is masked to 0. */
    uint64_t guest_cr4 = vmx_rdmsr(IA32_VMX_CR4_FIXED0);
    guest_cr4 |= CR4_VMXE;
    vmx_vmwrite(VMCS_GUEST_CR4, guest_cr4);

    /* EFER: all clear for real mode */
    vmx_vmwrite(VMCS_GUEST_IA32_EFER, 0);

    /* PAT: must be initialized if VM_ENTRY_LOAD_IA32_PAT is forced on */
    vmx_vmwrite(VMCS_GUEST_IA32_PAT, 0x0007040600070406ULL);

    /* Guest segments: architectural x86 reset state */
    /* CS reset hidden base = 0xFFFF0000, RIP = 0xFFF0 -> 0xFFFFFFF0 */
    vmx_vmwrite(VMCS_GUEST_CS_SEL,    0xF000);
    vmx_vmwrite(VMCS_GUEST_CS_BASE,   0xFFFF0000ULL);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT,  0xFFFF);
    vmx_vmwrite(VMCS_GUEST_CS_ACCESS, 0x009B);  /* present, RW, accessed, code */

    /* DS/ES/FS/GS/SS: base=0, limit=0xFFFF */
    uint32_t data_access = 0x0093;  /* present, RW, accessed, data */
    uint16_t data_sels[] = {0, 0, 0, 0, 0};
    /* DS */
    vmx_vmwrite(VMCS_GUEST_DS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS, data_access);
    /* ES */
    vmx_vmwrite(VMCS_GUEST_ES_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS, data_access);
    /* FS */
    vmx_vmwrite(VMCS_GUEST_FS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS, data_access);
    /* GS */
    vmx_vmwrite(VMCS_GUEST_GS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS, data_access);
    /* SS */
    vmx_vmwrite(VMCS_GUEST_SS_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS, data_access);

    /* LDTR: null and unusable */
    vmx_vmwrite(VMCS_GUEST_LDTR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS, 0x10000);

    /* TR: must be usable */
    vmx_vmwrite(VMCS_GUEST_TR_SEL, 0);
    vmx_vmwrite(VMCS_GUEST_TR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_TR_ACCESS, 0x008B);  /* present, busy TSS */

    /* GDTR/IDTR */
    vmx_vmwrite(VMCS_GUEST_GDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, 0xFFFF);
    vmx_vmwrite(VMCS_GUEST_IDTR_BASE, 0);
    vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, 0xFFFF);

    /* RIP = 0xFFF0 (reset vector: CS.base + RIP = 0xFFFFFFF0) */
    vmx_vmwrite(VMCS_GUEST_RIP, 0xFFF0);
    vmx_vmwrite(VMCS_GUEST_RSP, 0);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, 0x02);  /* bit 1 always set */

    /* DR7 */
    vmx_vmwrite(VMCS_GUEST_DR7, 0x400);

    /* VMCS link pointer: required to be -1 */
    vmx_vmwrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);

    /* Activity state: active */
    vmx_vmwrite(VMCS_GUEST_ACTIVITY_STATE, 0);
    vmx_vmwrite(VMCS_GUEST_INTERRUPTIBILITY, 0);
    vmx_vmwrite(VMCS_GUEST_PENDING_DBG_EXCEPT, 0);

    /* SYSENTER */
    vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, 0);
    vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, 0);

    vmx_vmwrite(VMCS_GUEST_IA32_DEBUGCTL, 0);

    /* Log guest state for debugging */
    serial_write_string("[VMX] Guest CR0=0x");
    serial_write_uint64(guest_cr0);
    serial_write_string(" CR4=0x");
    serial_write_uint64(vmx_vmread(VMCS_GUEST_CR4));
    serial_write_string("\n");

    (void)data_sels;
}

/* ── Register get/set ──────────────────────────────────────── */

int vmx_vcpu_set_regs(vmx_vcpu_t *vcpu, const vmx_regs_t *regs)
{
    if (!vcpu || !regs) return -1;
    vcpu->guest_regs = *regs;

    /* Load VMCS for this vCPU */
    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;
    __asm__ volatile("vmptrld %0" :: "m"(vmcs_phys) : "cc", "memory");

    vmx_vmwrite(VMCS_GUEST_RSP, regs->rsp);
    vmx_vmwrite(VMCS_GUEST_RIP, regs->rip);
    vmx_vmwrite(VMCS_GUEST_RFLAGS, regs->rflags | 0x02);
    return 0;
}

int vmx_vcpu_get_regs(vmx_vcpu_t *vcpu, vmx_regs_t *regs)
{
    if (!vcpu || !regs) return -1;

    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;
    __asm__ volatile("vmptrld %0" :: "m"(vmcs_phys) : "cc", "memory");

    *regs = vcpu->guest_regs;
    regs->rsp = vmx_vmread(VMCS_GUEST_RSP);
    regs->rip = vmx_vmread(VMCS_GUEST_RIP);
    regs->rflags = vmx_vmread(VMCS_GUEST_RFLAGS);
    return 0;
}

int vmx_vcpu_set_sregs(vmx_vcpu_t *vcpu, const vmx_sregs_t *sregs)
{
    if (!vcpu || !sregs) return -1;

    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;
    __asm__ volatile("vmptrld %0" :: "m"(vmcs_phys) : "cc", "memory");

    /* CR */
    uint64_t guest_cr0 = sregs->cr0 | vmx_rdmsr(IA32_VMX_CR0_FIXED0);
    guest_cr0 &= vmx_rdmsr(IA32_VMX_CR0_FIXED1);
    if (!(sregs->cr0 & CR0_PE)) guest_cr0 &= ~CR0_PE;
    if (!(sregs->cr0 & CR0_PG)) guest_cr0 &= ~CR0_PG;
    vmx_vmwrite(VMCS_GUEST_CR0, guest_cr0);

    vmx_vmwrite(VMCS_GUEST_CR3, sregs->cr3);

    uint64_t guest_cr4 = sregs->cr4 | vmx_rdmsr(IA32_VMX_CR4_FIXED0);
    guest_cr4 &= vmx_rdmsr(IA32_VMX_CR4_FIXED1);
    if (!(sregs->cr0 & CR0_PE)) {
        /* Keep VMXE set in hardware-visible guest CR4 and expose 0 via the
         * read shadow configured in vmx_setup_vmcs_controls(). */
        guest_cr4 |= CR4_VMXE;
    }
    vmx_vmwrite(VMCS_GUEST_CR4, guest_cr4);

    /* EFER */
    vmx_vmwrite(VMCS_GUEST_IA32_EFER, sregs->efer);

    /* Update entry controls for IA-32e mode */
    uint32_t entry = (uint32_t)vmx_vmread(VMCS_ENTRY_CONTROLS);
    if (sregs->efer & EFER_LMA) {
        entry |= VM_ENTRY_IA32E_MODE;
    } else {
        entry &= ~VM_ENTRY_IA32E_MODE;
    }
    vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry);

    /* CS */
    vmx_vmwrite(VMCS_GUEST_CS_SEL, sregs->cs.selector);
    vmx_vmwrite(VMCS_GUEST_CS_BASE, sregs->cs.base);
    vmx_vmwrite(VMCS_GUEST_CS_LIMIT, sregs->cs.limit);
    uint32_t cs_ar = sregs->cs.type | (sregs->cs.s << 4) |
                     (sregs->cs.dpl << 5) | (sregs->cs.present << 7) |
                     (sregs->cs.avl << 12) | (sregs->cs.l << 13) |
                     (sregs->cs.db << 14) | (sregs->cs.g << 15);
    if (sregs->cs.unusable) cs_ar |= (1 << 16);

    /* DS */
    vmx_vmwrite(VMCS_GUEST_DS_SEL, sregs->ds.selector);
    vmx_vmwrite(VMCS_GUEST_DS_BASE, sregs->ds.base);
    vmx_vmwrite(VMCS_GUEST_DS_LIMIT, sregs->ds.limit);
    uint32_t ds_ar = sregs->ds.type | (sregs->ds.s << 4) |
                     (sregs->ds.dpl << 5) | (sregs->ds.present << 7) |
                     (sregs->ds.avl << 12) | (sregs->ds.l << 13) |
                     (sregs->ds.db << 14) | (sregs->ds.g << 15);
    if (sregs->ds.unusable) ds_ar |= (1 << 16);

    /* ES */
    vmx_vmwrite(VMCS_GUEST_ES_SEL, sregs->es.selector);
    vmx_vmwrite(VMCS_GUEST_ES_BASE, sregs->es.base);
    vmx_vmwrite(VMCS_GUEST_ES_LIMIT, sregs->es.limit);
    uint32_t es_ar = sregs->es.type | (sregs->es.s << 4) |
                     (sregs->es.dpl << 5) | (sregs->es.present << 7) |
                     (sregs->es.avl << 12) | (sregs->es.l << 13) |
                     (sregs->es.db << 14) | (sregs->es.g << 15);
    if (sregs->es.unusable) es_ar |= (1 << 16);

    /* SS */
    vmx_vmwrite(VMCS_GUEST_SS_SEL, sregs->ss.selector);
    vmx_vmwrite(VMCS_GUEST_SS_BASE, sregs->ss.base);
    vmx_vmwrite(VMCS_GUEST_SS_LIMIT, sregs->ss.limit);
    uint32_t ss_ar = sregs->ss.type | (sregs->ss.s << 4) |
                     (sregs->ss.dpl << 5) | (sregs->ss.present << 7) |
                     (sregs->ss.avl << 12) | (sregs->ss.l << 13) |
                     (sregs->ss.db << 14) | (sregs->ss.g << 15);
    if (sregs->ss.unusable) ss_ar |= (1 << 16);

    /* FS */
    vmx_vmwrite(VMCS_GUEST_FS_SEL, sregs->fs.selector);
    vmx_vmwrite(VMCS_GUEST_FS_BASE, sregs->fs.base);
    vmx_vmwrite(VMCS_GUEST_FS_LIMIT, sregs->fs.limit);
    uint32_t fs_ar = sregs->fs.type | (sregs->fs.s << 4) |
                     (sregs->fs.dpl << 5) | (sregs->fs.present << 7) |
                     (sregs->fs.avl << 12) | (sregs->fs.l << 13) |
                     (sregs->fs.db << 14) | (sregs->fs.g << 15);
    if (sregs->fs.unusable) fs_ar |= (1 << 16);

    /* GS */
    vmx_vmwrite(VMCS_GUEST_GS_SEL, sregs->gs.selector);
    vmx_vmwrite(VMCS_GUEST_GS_BASE, sregs->gs.base);
    vmx_vmwrite(VMCS_GUEST_GS_LIMIT, sregs->gs.limit);
    uint32_t gs_ar = sregs->gs.type | (sregs->gs.s << 4) |
                     (sregs->gs.dpl << 5) | (sregs->gs.present << 7) |
                     (sregs->gs.avl << 12) | (sregs->gs.l << 13) |
                     (sregs->gs.db << 14) | (sregs->gs.g << 15);
    if (sregs->gs.unusable) gs_ar |= (1 << 16);

    /* TR */
    vmx_vmwrite(VMCS_GUEST_TR_SEL, sregs->tr.selector);
    vmx_vmwrite(VMCS_GUEST_TR_BASE, sregs->tr.base);
    vmx_vmwrite(VMCS_GUEST_TR_LIMIT, sregs->tr.limit);
    uint32_t tr_ar = sregs->tr.type | (sregs->tr.s << 4) |
                     (sregs->tr.dpl << 5) | (sregs->tr.present << 7) |
                     (sregs->tr.avl << 12) | (sregs->tr.l << 13) |
                     (sregs->tr.db << 14) | (sregs->tr.g << 15);
    if (sregs->tr.unusable) tr_ar |= (1 << 16);

    /* LDT */
    vmx_vmwrite(VMCS_GUEST_LDTR_SEL, sregs->ldt.selector);
    vmx_vmwrite(VMCS_GUEST_LDTR_BASE, sregs->ldt.base);
    vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, sregs->ldt.limit);
    uint32_t ldt_ar = sregs->ldt.type | (sregs->ldt.s << 4) |
                      (sregs->ldt.dpl << 5) | (sregs->ldt.present << 7) |
                      (sregs->ldt.avl << 12) | (sregs->ldt.l << 13) |
                      (sregs->ldt.db << 14) | (sregs->ldt.g << 15);
    if (sregs->ldt.unusable) ldt_ar |= (1 << 16);

    if (!(sregs->cr0 & CR0_PE)) {
        /* Unrestricted guest real mode constraints */
        cs_ar = 0x9b;
        ds_ar = es_ar = ss_ar = fs_ar = gs_ar = 0x93;
        tr_ar = 0x8b;
        ldt_ar = 0x10000;
        vmx_vmwrite(VMCS_GUEST_CS_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_DS_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_ES_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_SS_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_FS_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_GS_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_TR_LIMIT, 0xFFFF);
        vmx_vmwrite(VMCS_GUEST_LDTR_SEL, 0);
        vmx_vmwrite(VMCS_GUEST_LDTR_BASE, 0);
        vmx_vmwrite(VMCS_GUEST_LDTR_LIMIT, 0);
    }

    vmx_vmwrite(VMCS_GUEST_CS_ACCESS, cs_ar);
    vmx_vmwrite(VMCS_GUEST_DS_ACCESS, ds_ar);
    vmx_vmwrite(VMCS_GUEST_ES_ACCESS, es_ar);
    vmx_vmwrite(VMCS_GUEST_SS_ACCESS, ss_ar);
    vmx_vmwrite(VMCS_GUEST_FS_ACCESS, fs_ar);
    vmx_vmwrite(VMCS_GUEST_GS_ACCESS, gs_ar);
    vmx_vmwrite(VMCS_GUEST_TR_ACCESS, tr_ar);
    vmx_vmwrite(VMCS_GUEST_LDTR_ACCESS, ldt_ar);

    /* GDT/IDT */
    vmx_vmwrite(VMCS_GUEST_GDTR_BASE, sregs->gdt.base);
    vmx_vmwrite(VMCS_GUEST_GDTR_LIMIT, sregs->gdt.limit);
    vmx_vmwrite(VMCS_GUEST_IDTR_BASE, sregs->idt.base);
    vmx_vmwrite(VMCS_GUEST_IDTR_LIMIT, sregs->idt.limit);

    return 0;
}

int vmx_vcpu_get_sregs(vmx_vcpu_t *vcpu, vmx_sregs_t *sregs)
{
    if (!vcpu || !sregs) return -1;
    memset(sregs, 0, sizeof(*sregs));

    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;
    __asm__ volatile("vmptrld %0" :: "m"(vmcs_phys) : "cc", "memory");

    sregs->cr0 = vmx_vmread(VMCS_GUEST_CR0);
    sregs->cr3 = vmx_vmread(VMCS_GUEST_CR3);
    sregs->cr4 = vmx_vmread(VMCS_GUEST_CR4);
    sregs->efer = vmx_vmread(VMCS_GUEST_IA32_EFER);

    /* CS */
    sregs->cs.selector = (uint16_t)vmx_vmread(VMCS_GUEST_CS_SEL);
    sregs->cs.base = vmx_vmread(VMCS_GUEST_CS_BASE);
    sregs->cs.limit = (uint32_t)vmx_vmread(VMCS_GUEST_CS_LIMIT);
    uint32_t ar = (uint32_t)vmx_vmread(VMCS_GUEST_CS_ACCESS);
    sregs->cs.type = ar & 0xF;
    sregs->cs.s = (ar >> 4) & 1;
    sregs->cs.dpl = (ar >> 5) & 3;
    sregs->cs.present = (ar >> 7) & 1;
    sregs->cs.avl = (ar >> 12) & 1;
    sregs->cs.l = (ar >> 13) & 1;
    sregs->cs.db = (ar >> 14) & 1;
    sregs->cs.g = (ar >> 15) & 1;
    sregs->cs.unusable = (ar >> 16) & 1;

    /* GDT/IDT */
    sregs->gdt.base = vmx_vmread(VMCS_GUEST_GDTR_BASE);
    sregs->gdt.limit = (uint16_t)vmx_vmread(VMCS_GUEST_GDTR_LIMIT);
    sregs->idt.base = vmx_vmread(VMCS_GUEST_IDTR_BASE);
    sregs->idt.limit = (uint16_t)vmx_vmread(VMCS_GUEST_IDTR_LIMIT);

    /* Helper macro to extract AR bits */
    #define GET_AR(field, target) do { \
        uint32_t ar_val = (uint32_t)vmx_vmread(field ## _ACCESS); \
        (target).selector = (uint16_t)vmx_vmread(field ## _SEL); \
        (target).base = vmx_vmread(field ## _BASE); \
        (target).limit = (uint32_t)vmx_vmread(field ## _LIMIT); \
        (target).type = ar_val & 0xF; \
        (target).s = (ar_val >> 4) & 1; \
        (target).dpl = (ar_val >> 5) & 3; \
        (target).present = (ar_val >> 7) & 1; \
        (target).avl = (ar_val >> 12) & 1; \
        (target).l = (ar_val >> 13) & 1; \
        (target).db = (ar_val >> 14) & 1; \
        (target).g = (ar_val >> 15) & 1; \
        (target).unusable = (ar_val >> 16) & 1; \
    } while(0)

    GET_AR(VMCS_GUEST_DS, sregs->ds);
    GET_AR(VMCS_GUEST_ES, sregs->es);
    GET_AR(VMCS_GUEST_FS, sregs->fs);
    GET_AR(VMCS_GUEST_GS, sregs->gs);
    GET_AR(VMCS_GUEST_SS, sregs->ss);
    GET_AR(VMCS_GUEST_TR, sregs->tr);
    GET_AR(VMCS_GUEST_LDTR, sregs->ldt);

    #undef GET_AR

    return 0;
}

/* ── Memory slot management ────────────────────────────────── */

int vmx_vcpu_add_mem_slot(vmx_vcpu_t *vcpu, const kvm_userspace_memory_region_t *region)
{
    if (!vcpu || !region) return -1;
    if (vcpu->mem_slot_count >= VMX_MAX_MEM_SLOTS) {
        serial_write_string("[VMX] Too many memory slots\n");
        return -1;
    }

    vmx_mem_slot_t *slot = &vcpu->mem_slots[vcpu->mem_slot_count];
    slot->slot = region->slot;
    slot->guest_phys_addr = region->guest_phys_addr;
    slot->memory_size = region->memory_size;
    slot->host_virt_addr = region->userspace_addr;

    /* Map into EPT */
    int rc = ept_map_range(vcpu,
                           region->guest_phys_addr,
                           region->userspace_addr,  /* identity mapped kernel */
                           region->memory_size,
                           EPT_RWX | (EPT_MT_WB << 3));
    if (rc < 0) {
        serial_write_string("[VMX] EPT map failed for slot\n");
        return -1;
    }

    vcpu->mem_slot_count++;
    serial_write_string("[VMX] Memory slot added: GPA=");
    serial_write_uint64(region->guest_phys_addr);
    serial_write_string(" size=");
    serial_write_uint64(region->memory_size);
    serial_write_string("\n");
    return 0;
}

/* ── vCPU run ──────────────────────────────────────────────── */

int vmx_vcpu_run(vmx_vcpu_t *vcpu)
{
    if (!vcpu || !vcpu->active) return -1;

    /* Reload VMCS */
    uint64_t vmcs_phys = (uint64_t)(uintptr_t)vcpu->vmcs_region;
    __asm__ volatile("vmptrld %0" :: "m"(vmcs_phys) : "cc", "memory");

    /* IO IN re-entry: after userland emulated an IN instruction,
     * copy the result from io_data back into guest RAX */
    if (vcpu->launched && vcpu->run->exit_reason == KVM_EXIT_IO &&
        vcpu->run->io.direction == 1) {
        uint64_t val = 0;
        memcpy(&val, vcpu->run->io_data, vcpu->run->io.size);
        uint64_t mask = (vcpu->run->io.size >= 4) ? 0xFFFFFFFFULL :
                        ((1ULL << (vcpu->run->io.size * 8)) - 1);
        vcpu->guest_regs.rax = (vcpu->guest_regs.rax & ~mask) | (val & mask);
    }

    /* MMIO read re-entry: after userland emulated an MMIO read,
     * copy the result into guest RAX */
    if (vcpu->launched && vcpu->run->exit_reason == KVM_EXIT_MMIO &&
        !vcpu->run->mmio.is_write) {
        uint64_t val = 0;
        memcpy(&val, vcpu->run->mmio.data, vcpu->run->mmio.len);
        vcpu->guest_regs.rax = val;
    }

    /* Clear exit reason to avoid accidental re-entry processing inside the loop */
    vcpu->run->exit_reason = KVM_EXIT_UNKNOWN;

    while (1) {
        int rc;
        if (!vcpu->launched) {
            rc = vmx_vmlaunch_asm(&vcpu->guest_regs);
            if (rc == 0) {
                vcpu->launched = 1;
            }
        } else {
            rc = vmx_vmresume_asm(&vcpu->guest_regs);
        }

        if (rc != 0) {
            /* VMLAUNCH/VMRESUME failed */
            uint64_t vm_err = vmx_vmread(0x4400); /* VM_INSTRUCTION_ERROR */
            serial_write_string("[VMX] VM entry failed, rc=");
            serial_write_uint32((uint32_t)rc);
            serial_write_string(" VM_INSTR_ERR=");
            serial_write_uint32((uint32_t)vm_err);
            serial_write_string("\n");

            /* Dump VMCS guest state for diagnosis */
            serial_write_string("[VMX] VMCS Dump:\n");
            serial_write_string("[VMX]   CR0=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_CR0));
            serial_write_string(" CR3=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_CR3));
            serial_write_string(" CR4=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_CR4));
            serial_write_string("\n");
            serial_write_string("[VMX]   EFER=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_IA32_EFER));
            serial_write_string(" PAT=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_IA32_PAT));
            serial_write_string("\n");
            serial_write_string("[VMX]   Entry=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_ENTRY_CONTROLS));
            serial_write_string(" Exit=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_EXIT_CONTROLS));
            serial_write_string("\n");
            serial_write_string("[VMX]   CS: sel=0x");
            serial_write_uint16((uint16_t)vmx_vmread(VMCS_GUEST_CS_SEL));
            serial_write_string(" base=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_CS_BASE));
            serial_write_string(" lim=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_CS_LIMIT));
            serial_write_string(" ar=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_CS_ACCESS));
            serial_write_string("\n");
            serial_write_string("[VMX]   SS: sel=0x");
            serial_write_uint16((uint16_t)vmx_vmread(VMCS_GUEST_SS_SEL));
            serial_write_string(" base=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_SS_BASE));
            serial_write_string(" ar=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_SS_ACCESS));
            serial_write_string("\n");
            serial_write_string("[VMX]   DS: ar=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_DS_ACCESS));
            serial_write_string(" ES: ar=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_ES_ACCESS));
            serial_write_string("\n");
            serial_write_string("[VMX]   TR: sel=0x");
            serial_write_uint16((uint16_t)vmx_vmread(VMCS_GUEST_TR_SEL));
            serial_write_string(" ar=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_TR_ACCESS));
            serial_write_string(" LDTR: ar=0x");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_LDTR_ACCESS));
            serial_write_string("\n");
            serial_write_string("[VMX]   RIP=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_RIP));
            serial_write_string(" RSP=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_RSP));
            serial_write_string(" RFLAGS=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_RFLAGS));
            serial_write_string("\n");
            serial_write_string("[VMX]   Activity=");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_ACTIVITY_STATE));
            serial_write_string(" Interruptibility=");
            serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY));
            serial_write_string(" LinkPtr=0x");
            serial_write_uint64(vmx_vmread(VMCS_GUEST_VMCS_LINK_PTR));
            serial_write_string("\n");

            vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
            vcpu->run->internal.suberror = (uint32_t)vm_err;
            return -1;
        }

        /* VM Exit occurred — handle it */
        int exit_rc = vmx_handle_exit(vcpu);
        if (exit_rc != 0) {
            return exit_rc;
        }
    }
}

/* ── VM Exit handler ───────────────────────────────────────── */

static int vmx_handle_exit(vmx_vcpu_t *vcpu)
{
    uint32_t exit_reason = (uint32_t)vmx_vmread(VMCS_EXIT_REASON) & 0xFFFF;
    uint64_t exit_qual   = vmx_vmread(VMCS_EXIT_QUALIFICATION);

    switch (exit_reason) {
    case EXIT_REASON_CPUID:
        vmx_handle_cpuid(vcpu);
        return 0;  /* continue running */

    case EXIT_REASON_IO_INSTRUCTION:
        return vmx_handle_io(vcpu);

    case EXIT_REASON_MSR_READ:
        return vmx_handle_msr_read(vcpu);

    case EXIT_REASON_MSR_WRITE:
        return vmx_handle_msr_write(vcpu);

    case EXIT_REASON_EPT_VIOLATION:
        return vmx_handle_ept_violation(vcpu);

    case EXIT_REASON_EPT_MISCONFIG:
        serial_write_string("[VMX] EPT misconfig at GPA=");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_PHYS_ADDR));
        serial_write_string("\n");
        vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
        vcpu->run->internal.suberror = 2;
        return 1;

    case EXIT_REASON_CR_ACCESS:
        return vmx_handle_cr_access(vcpu);

    case EXIT_REASON_HLT:
        vcpu->run->exit_reason = KVM_EXIT_HLT;
        return 1;  /* exit to userland */

    case EXIT_REASON_TRIPLE_FAULT:
        serial_write_string("[VMX] Triple fault!\n");
        serial_write_string("[VMX]   RIP=");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_RIP));
        serial_write_string(" CR0=");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_CR0));
        serial_write_string("\n");
        vcpu->run->exit_reason = KVM_EXIT_SHUTDOWN;
        return 1;

    case EXIT_REASON_VMCALL:
        /* Skip the VMCALL instruction */
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 0;

    case EXIT_REASON_XSETBV: {
        /* Allow guest to set XCR0 */
        uint32_t xcr = (uint32_t)vcpu->guest_regs.rcx;
        uint64_t val = (vcpu->guest_regs.rdx << 32) | (vcpu->guest_regs.rax & 0xFFFFFFFF);
        if (xcr == 0) {
            __asm__ volatile("xsetbv" :: "c"(xcr),
                             "a"((uint32_t)val),
                             "d"((uint32_t)(val >> 32)));
        }
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 0;
    }

    case EXIT_REASON_WBINVD:
        __asm__ volatile("wbinvd");
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 0;

    case EXIT_REASON_EXTERNAL_INT:
    case EXIT_REASON_EXCEPTION_NMI:
        /* These are host interrupts; just re-enter guest */
        return 0;

    case EXIT_REASON_PAUSE:
    case EXIT_REASON_MONITOR:
    case EXIT_REASON_MWAIT:
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 0;

    case EXIT_REASON_INVLPG:
    case EXIT_REASON_RDTSC:
    case EXIT_REASON_RDTSCP:
    case EXIT_REASON_INVEPT:
    case EXIT_REASON_INVVPID:
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 0;

    case EXIT_REASON_DR_ACCESS:
        /* Allow guest DR access by skipping the instruction */
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 0;

    case EXIT_REASON_INVALID_GUEST_STATE:
        serial_write_string("[VMX] VM entry failure: Invalid Guest State (33)\n");
        serial_write_string("[VMX] VMCS Dump:\n");
        serial_write_string("[VMX]   CR0=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_CR0));
        serial_write_string(" CR3=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_CR3));
        serial_write_string(" CR4=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_CR4));
        serial_write_string("\n");
        serial_write_string("[VMX]   EFER=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_IA32_EFER));
        serial_write_string(" PAT=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_IA32_PAT));
        serial_write_string("\n");
        serial_write_string("[VMX]   Entry=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_ENTRY_CONTROLS));
        serial_write_string(" Exit=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_EXIT_CONTROLS));
        serial_write_string("\n");
        serial_write_string("[VMX]   CS: sel=0x");
        serial_write_uint16((uint16_t)vmx_vmread(VMCS_GUEST_CS_SEL));
        serial_write_string(" base=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_CS_BASE));
        serial_write_string(" lim=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_CS_LIMIT));
        serial_write_string(" ar=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_CS_ACCESS));
        serial_write_string("\n");
        serial_write_string("[VMX]   SS: sel=0x");
        serial_write_uint16((uint16_t)vmx_vmread(VMCS_GUEST_SS_SEL));
        serial_write_string(" base=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_SS_BASE));
        serial_write_string(" ar=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_SS_ACCESS));
        serial_write_string("\n");
        serial_write_string("[VMX]   DS: ar=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_DS_ACCESS));
        serial_write_string(" ES: ar=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_ES_ACCESS));
        serial_write_string("\n");
        serial_write_string("[VMX]   TR: sel=0x");
        serial_write_uint16((uint16_t)vmx_vmread(VMCS_GUEST_TR_SEL));
        serial_write_string(" ar=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_TR_ACCESS));
        serial_write_string(" LDTR: ar=0x");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_LDTR_ACCESS));
        serial_write_string("\n");
        serial_write_string("[VMX]   RIP=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_RIP));
        serial_write_string(" RSP=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_RSP));
        serial_write_string(" RFLAGS=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_RFLAGS));
        serial_write_string("\n");
        serial_write_string("[VMX]   Activity=");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_ACTIVITY_STATE));
        serial_write_string(" Interruptibility=");
        serial_write_uint32((uint32_t)vmx_vmread(VMCS_GUEST_INTERRUPTIBILITY));
        serial_write_string(" LinkPtr=0x");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_VMCS_LINK_PTR));
        serial_write_string("\n");
        vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
        vcpu->run->internal.suberror = exit_reason;
        return 1;

    default:
        serial_write_string("[VMX] Unhandled exit reason=");
        serial_write_uint32(exit_reason);
        serial_write_string(" qual=");
        serial_write_uint64(exit_qual);
        serial_write_string(" RIP=");
        serial_write_uint64(vmx_vmread(VMCS_GUEST_RIP));
        serial_write_string("\n");
        vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
        vcpu->run->internal.suberror = exit_reason;
        return 1;
    }

    (void)exit_qual;
}

/* ── CPUID handler ─────────────────────────────────────────── */

static void vmx_handle_cpuid(vmx_vcpu_t *vcpu)
{
    uint32_t eax = (uint32_t)vcpu->guest_regs.rax;
    uint32_t ecx = (uint32_t)vcpu->guest_regs.rcx;
    uint32_t out_eax, out_ebx, out_ecx, out_edx;

    __asm__ volatile("cpuid"
        : "=a"(out_eax), "=b"(out_ebx), "=c"(out_ecx), "=d"(out_edx)
        : "a"(eax), "c"(ecx));

    /* Mask out VMX capability from guest */
    if (eax == 1) {
        out_ecx &= ~(1U << 5);   /* clear VMX bit */
        out_ecx &= ~(1U << 31);  /* clear hypervisor bit */
    }

    vcpu->guest_regs.rax = out_eax;
    vcpu->guest_regs.rbx = out_ebx;
    vcpu->guest_regs.rcx = out_ecx;
    vcpu->guest_regs.rdx = out_edx;

    /* Advance RIP past CPUID */
    vmx_vmwrite(VMCS_GUEST_RIP,
                vmx_vmread(VMCS_GUEST_RIP) +
                vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
}

/* ── IO handler ────────────────────────────────────────────── */

static int vmx_handle_io(vmx_vcpu_t *vcpu)
{
    uint64_t qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);
    uint16_t port   = (uint16_t)((qual >> 16) & 0xFFFF);
    uint8_t  size   = (uint8_t)((qual & 7) + 1);  /* 0=1byte, 1=2, 3=4 */
    uint8_t  is_in  = (qual >> 3) & 1;
    uint8_t  is_str = (qual >> 4) & 1;

    if (is_str) {
        /* String IO — pass to userland */
        vcpu->run->exit_reason = KVM_EXIT_IO;
        vcpu->run->io.direction = is_in;
        vcpu->run->io.size = size;
        vcpu->run->io.port = port;
        vcpu->run->io.count = 1;
        vcpu->run->io.data_offset = KVM_IO_DATA_OFFSET;

        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) +
                     vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
        return 1;
    }

    /* Fill kvm_run for userland */
    vcpu->run->exit_reason = KVM_EXIT_IO;
    vcpu->run->io.direction = is_in;
    vcpu->run->io.size = size;
    vcpu->run->io.port = port;
    vcpu->run->io.count = 1;
    vcpu->run->io.data_offset = KVM_IO_DATA_OFFSET;

    /* For OUT, copy data from guest rax */
    if (!is_in) {
        uint32_t val = (uint32_t)vcpu->guest_regs.rax;
        memcpy(vcpu->run->io_data, &val, size);
    }

    /* Advance RIP */
    vmx_vmwrite(VMCS_GUEST_RIP,
                vmx_vmread(VMCS_GUEST_RIP) +
                vmx_vmread(VMCS_EXIT_INSTR_LENGTH));

    return 1;  /* exit to userland for IO emulation */
}

/* ── MSR read handler ──────────────────────────────────────── */

static int vmx_handle_msr_read(vmx_vcpu_t *vcpu)
{
    uint32_t msr = (uint32_t)vcpu->guest_regs.rcx;
    uint64_t val = 0;

    switch (msr) {
    case IA32_EFER_MSR:
        val = vmx_vmread(VMCS_GUEST_IA32_EFER);
        break;
    case IA32_APIC_BASE_MSR:
        val = 0xFEE00000ULL | (1ULL << 11); /* APIC enabled, BSP */
        break;
    case IA32_MTRRCAP:
        val = (uint64_t)MTRR_VAR_COUNT | (1ULL << 8) | (1ULL << 10); /* VCNT=8, FIX=1, WC=1 */
        break;
    case IA32_MTRR_DEF_TYPE:
        val = g_mtrr_def_type;
        break;
    case IA32_PAT_MSR:
        val = g_guest_pat;
        break;
    case IA32_FS_BASE_MSR:
        val = vmx_vmread(VMCS_GUEST_FS_BASE);
        break;
    case IA32_GS_BASE_MSR:
        val = vmx_vmread(VMCS_GUEST_GS_BASE);
        break;
    case IA32_KERNEL_GS_BASE_MSR:
        val = 0;
        break;
    case IA32_TSC_AUX:
        val = 0;
        break;
    case IA32_SYSENTER_CS:
        val = vmx_vmread(VMCS_GUEST_SYSENTER_CS);
        break;
    case IA32_SYSENTER_ESP:
        val = vmx_vmread(VMCS_GUEST_SYSENTER_ESP);
        break;
    case IA32_SYSENTER_EIP:
        val = vmx_vmread(VMCS_GUEST_SYSENTER_EIP);
        break;
    default:
        /* MTRR variable range MSRs: 0x200-0x20F */
        if (msr >= 0x200 && msr <= 0x20F) {
            uint32_t idx = (msr - 0x200) / 2;
            if (idx < MTRR_VAR_COUNT) {
                val = (msr & 1) ? g_mtrr_var_mask[idx] : g_mtrr_var_base[idx];
            }
            break;
        }
        /* MTRR fixed range MSRs: absorb with UC */
        if (msr == 0x250 || msr == 0x258 || msr == 0x259 ||
            (msr >= 0x268 && msr <= 0x26F)) {
            val = 0;
            break;
        }
        /* Unknown MSR: return 0 and log */
        serial_write_string("[VMX] RDMSR unknown: 0x");
        serial_write_uint32(msr);
        serial_write_string("\n");
        val = 0;
        break;
    }

    vcpu->guest_regs.rax = val & 0xFFFFFFFF;
    vcpu->guest_regs.rdx = (val >> 32) & 0xFFFFFFFF;

    vmx_vmwrite(VMCS_GUEST_RIP,
                vmx_vmread(VMCS_GUEST_RIP) +
                vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
    return 0;
}

/* ── MSR write handler ─────────────────────────────────────── */

static int vmx_handle_msr_write(vmx_vcpu_t *vcpu)
{
    uint32_t msr = (uint32_t)vcpu->guest_regs.rcx;
    uint64_t val = ((vcpu->guest_regs.rdx & 0xFFFFFFFF) << 32) |
                   (vcpu->guest_regs.rax & 0xFFFFFFFF);

    switch (msr) {
    case IA32_EFER_MSR:
        vmx_vmwrite(VMCS_GUEST_IA32_EFER, val);
        /* Update entry controls for long mode */
        {
            uint32_t entry = (uint32_t)vmx_vmread(VMCS_ENTRY_CONTROLS);
            if (val & EFER_LMA) {
                entry |= VM_ENTRY_IA32E_MODE;
            } else {
                entry &= ~VM_ENTRY_IA32E_MODE;
            }
            vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry);
        }
        break;
    case IA32_APIC_BASE_MSR:
        /* Ignore APIC base writes for now */
        break;
    case IA32_MTRR_DEF_TYPE:
        g_mtrr_def_type = val;
        break;
    case IA32_PAT_MSR:
        g_guest_pat = val;
        vmx_vmwrite(VMCS_GUEST_IA32_PAT, val);
        break;
    case IA32_FS_BASE_MSR:
        vmx_vmwrite(VMCS_GUEST_FS_BASE, val);
        break;
    case IA32_GS_BASE_MSR:
        vmx_vmwrite(VMCS_GUEST_GS_BASE, val);
        break;
    case IA32_KERNEL_GS_BASE_MSR:
        /* Absorb */
        break;
    case IA32_TSC_AUX:
        /* Absorb */
        break;
    case IA32_SYSENTER_CS:
        vmx_vmwrite(VMCS_GUEST_SYSENTER_CS, val);
        break;
    case IA32_SYSENTER_ESP:
        vmx_vmwrite(VMCS_GUEST_SYSENTER_ESP, val);
        break;
    case IA32_SYSENTER_EIP:
        vmx_vmwrite(VMCS_GUEST_SYSENTER_EIP, val);
        break;
    default:
        /* MTRR variable range MSRs: 0x200-0x20F */
        if (msr >= 0x200 && msr <= 0x20F) {
            uint32_t idx = (msr - 0x200) / 2;
            if (idx < MTRR_VAR_COUNT) {
                if (msr & 1) {
                    g_mtrr_var_mask[idx] = val;
                } else {
                    g_mtrr_var_base[idx] = val;
                }
            }
            break;
        }
        /* MTRR fixed range MSRs: absorb */
        if (msr == 0x250 || msr == 0x258 || msr == 0x259 ||
            (msr >= 0x268 && msr <= 0x26F)) {
            break;
        }
        serial_write_string("[VMX] WRMSR unknown: 0x");
        serial_write_uint32(msr);
        serial_write_string(" val=");
        serial_write_uint64(val);
        serial_write_string("\n");
        break;
    }

    vmx_vmwrite(VMCS_GUEST_RIP,
                vmx_vmread(VMCS_GUEST_RIP) +
                vmx_vmread(VMCS_EXIT_INSTR_LENGTH));
    return 0;
}

/* ── EPT violation handler ─────────────────────────────────── */

static int vmx_handle_ept_violation(vmx_vcpu_t *vcpu)
{
    uint64_t gpa  = vmx_vmread(VMCS_GUEST_PHYS_ADDR);
    uint64_t qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);

    uint8_t is_read  = (qual >> 0) & 1;
    uint8_t is_write = (qual >> 1) & 1;
    uint8_t is_exec  = (qual >> 2) & 1;

    /* Report as MMIO to userland */
    vcpu->run->exit_reason = KVM_EXIT_MMIO;
    vcpu->run->mmio.phys_addr = gpa;
    vcpu->run->mmio.is_write = is_write;

    /* Advance RIP past the faulting instruction */
    uint32_t instr_len = (uint32_t)vmx_vmread(VMCS_EXIT_INSTR_LENGTH);
    if (instr_len > 0 && instr_len <= 15) {
        vmx_vmwrite(VMCS_GUEST_RIP,
                     vmx_vmread(VMCS_GUEST_RIP) + instr_len);
    }

    /* For simple MMIO: set default length */
    vcpu->run->mmio.len = 4;
    memset(vcpu->run->mmio.data, 0, 8);

    if (is_write) {
        /* Data comes from the guest; approximate from rax */
        uint64_t val = vcpu->guest_regs.rax;
        memcpy(vcpu->run->mmio.data, &val, 8);
    }

    (void)is_read;
    (void)is_exec;
    return 1;  /* exit to userland */
}

/* ── CR access handler ─────────────────────────────────────── */

static int vmx_handle_cr_access(vmx_vcpu_t *vcpu)
{
    uint64_t qual = vmx_vmread(VMCS_EXIT_QUALIFICATION);
    uint32_t cr_num  = (uint32_t)(qual & 0xF);
    uint32_t access  = (uint32_t)((qual >> 4) & 3);
    uint32_t reg     = (uint32_t)((qual >> 8) & 0xF);

    /* Get value from the source register */
    uint64_t *gprs = (uint64_t *)&vcpu->guest_regs;
    uint64_t val = gprs[reg]; /* rax=0, rcx=2, rdx=3, rbx=1, rsp=7... */

    /* Map VMCS exit qual register encoding to our struct layout */
    /* Qual encoding: 0=rax,1=rcx,2=rdx,3=rbx,4=rsp,5=rbp,6=rsi,7=rdi,8-15=r8-r15 */
    uint64_t reg_val = 0;
    switch (reg) {
    case 0:  reg_val = vcpu->guest_regs.rax; break;
    case 1:  reg_val = vcpu->guest_regs.rcx; break;
    case 2:  reg_val = vcpu->guest_regs.rdx; break;
    case 3:  reg_val = vcpu->guest_regs.rbx; break;
    case 4:  reg_val = vmx_vmread(VMCS_GUEST_RSP); break;
    case 5:  reg_val = vcpu->guest_regs.rbp; break;
    case 6:  reg_val = vcpu->guest_regs.rsi; break;
    case 7:  reg_val = vcpu->guest_regs.rdi; break;
    case 8:  reg_val = vcpu->guest_regs.r8;  break;
    case 9:  reg_val = vcpu->guest_regs.r9;  break;
    case 10: reg_val = vcpu->guest_regs.r10; break;
    case 11: reg_val = vcpu->guest_regs.r11; break;
    case 12: reg_val = vcpu->guest_regs.r12; break;
    case 13: reg_val = vcpu->guest_regs.r13; break;
    case 14: reg_val = vcpu->guest_regs.r14; break;
    case 15: reg_val = vcpu->guest_regs.r15; break;
    }

    if (access == 0) {
        /* MOV to CR */
        if (cr_num == 0) {
            vmx_vmwrite(VMCS_GUEST_CR0, reg_val);
        } else if (cr_num == 3) {
            vmx_vmwrite(VMCS_GUEST_CR3, reg_val);
        } else if (cr_num == 4) {
            vmx_vmwrite(VMCS_GUEST_CR4, reg_val);
        }
    } else if (access == 1) {
        /* MOV from CR */
        uint64_t cr_val = 0;
        if (cr_num == 0) cr_val = vmx_vmread(VMCS_GUEST_CR0);
        else if (cr_num == 3) cr_val = vmx_vmread(VMCS_GUEST_CR3);
        else if (cr_num == 4) cr_val = vmx_vmread(VMCS_GUEST_CR4);

        switch (reg) {
        case 0:  vcpu->guest_regs.rax = cr_val; break;
        case 1:  vcpu->guest_regs.rcx = cr_val; break;
        case 2:  vcpu->guest_regs.rdx = cr_val; break;
        case 3:  vcpu->guest_regs.rbx = cr_val; break;
        case 4:  vmx_vmwrite(VMCS_GUEST_RSP, cr_val); break;
        case 5:  vcpu->guest_regs.rbp = cr_val; break;
        case 6:  vcpu->guest_regs.rsi = cr_val; break;
        case 7:  vcpu->guest_regs.rdi = cr_val; break;
        case 8:  vcpu->guest_regs.r8  = cr_val; break;
        case 9:  vcpu->guest_regs.r9  = cr_val; break;
        case 10: vcpu->guest_regs.r10 = cr_val; break;
        case 11: vcpu->guest_regs.r11 = cr_val; break;
        case 12: vcpu->guest_regs.r12 = cr_val; break;
        case 13: vcpu->guest_regs.r13 = cr_val; break;
        case 14: vcpu->guest_regs.r14 = cr_val; break;
        case 15: vcpu->guest_regs.r15 = cr_val; break;
        }
    } else if (access == 2) {
        /* CLTS */
        uint64_t cr0 = vmx_vmread(VMCS_GUEST_CR0);
        cr0 &= ~CR0_TS;
        vmx_vmwrite(VMCS_GUEST_CR0, cr0);
    } else if (access == 3) {
        /* LMSW */
        uint16_t msw = (uint16_t)(qual >> 16);
        uint64_t cr0 = vmx_vmread(VMCS_GUEST_CR0);
        cr0 = (cr0 & 0xFFFFFFFFFFFF0000ULL) | msw;
        vmx_vmwrite(VMCS_GUEST_CR0, cr0);
    }

    vmx_vmwrite(VMCS_GUEST_RIP,
                vmx_vmread(VMCS_GUEST_RIP) +
                vmx_vmread(VMCS_EXIT_INSTR_LENGTH));

    (void)val;
    return 0;
}
