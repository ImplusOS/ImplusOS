#pragma once

#include <stdint.h>
#include <stddef.h>

#define IA32_FEATURE_CONTROL        0x003A
#define IA32_VMX_BASIC              0x0480
#define IA32_VMX_PINBASED_CTLS      0x0481
#define IA32_VMX_PROCBASED_CTLS     0x0482
#define IA32_VMX_EXIT_CTLS          0x0483
#define IA32_VMX_ENTRY_CTLS         0x0484
#define IA32_VMX_PROCBASED_CTLS2    0x048B
#define IA32_VMX_EPT_VPID_CAP      0x048C
#define IA32_VMX_TRUE_PINBASED_CTLS 0x048D
#define IA32_VMX_TRUE_PROCBASED_CTLS 0x048E
#define IA32_VMX_TRUE_EXIT_CTLS     0x048F
#define IA32_VMX_TRUE_ENTRY_CTLS    0x0490
#define IA32_VMX_CR0_FIXED0        0x0486
#define IA32_VMX_CR0_FIXED1        0x0487
#define IA32_VMX_CR4_FIXED0        0x0488
#define IA32_VMX_CR4_FIXED1        0x0489

#define FEATURE_CONTROL_LOCKED      (1ULL << 0)
#define FEATURE_CONTROL_VMXON       (1ULL << 2)

#define CR0_PE   (1ULL << 0)
#define CR0_MP   (1ULL << 1)
#define CR0_NE   (1ULL << 5)
#define CR0_WP   (1ULL << 16)
#define CR0_PG   (1ULL << 31)

#define CR4_VMXE (1ULL << 13)
#define CR4_PAE  (1ULL << 5)

#define VMCS_GUEST_ES_SELECTOR      0x0800
#define VMCS_GUEST_CS_SELECTOR      0x0802
#define VMCS_GUEST_SS_SELECTOR      0x0804
#define VMCS_GUEST_DS_SELECTOR      0x0806
#define VMCS_GUEST_FS_SELECTOR      0x0808
#define VMCS_GUEST_GS_SELECTOR      0x080A
#define VMCS_GUEST_LDTR_SELECTOR    0x080C
#define VMCS_GUEST_TR_SELECTOR      0x080E

#define VMCS_HOST_ES_SELECTOR       0x0C00
#define VMCS_HOST_CS_SELECTOR       0x0C02
#define VMCS_HOST_SS_SELECTOR       0x0C04
#define VMCS_HOST_DS_SELECTOR       0x0C06
#define VMCS_HOST_FS_SELECTOR       0x0C08
#define VMCS_HOST_GS_SELECTOR       0x0C0A
#define VMCS_HOST_TR_SELECTOR       0x0C0C

#define VMCS_PIN_BASED_CONTROLS     0x4000
#define VMCS_PROC_BASED_CONTROLS    0x4002
#define VMCS_EXCEPTION_BITMAP       0x4004
#define VMCS_EXIT_CONTROLS          0x400C
#define VMCS_ENTRY_CONTROLS         0x4012
#define VMCS_PROC_BASED_CONTROLS2   0x401E
#define VMCS_ENTRY_INTR_INFO        0x4016
#define VMCS_ENTRY_EXCEPTION_ERRCODE 0x4018
#define VMCS_ENTRY_INSTRUCTION_LEN  0x401A

#define VMCS_EXIT_REASON            0x4402
#define VMCS_EXIT_INTR_INFO         0x4404
#define VMCS_EXIT_INTR_ERROR_CODE   0x4406
#define VMCS_IDT_VECTORING_INFO     0x4408
#define VMCS_EXIT_INSTRUCTION_LEN   0x440C
#define VMCS_EXIT_INSTRUCTION_INFO  0x440E
#define VMCS_EXIT_QUALIFICATION     0x6400

#define VMCS_GUEST_ES_LIMIT         0x4800
#define VMCS_GUEST_CS_LIMIT         0x4802
#define VMCS_GUEST_SS_LIMIT         0x4804
#define VMCS_GUEST_DS_LIMIT         0x4806
#define VMCS_GUEST_FS_LIMIT         0x4808
#define VMCS_GUEST_GS_LIMIT         0x480A
#define VMCS_GUEST_LDTR_LIMIT       0x480C
#define VMCS_GUEST_TR_LIMIT         0x480E
#define VMCS_GUEST_GDTR_LIMIT       0x4810
#define VMCS_GUEST_IDTR_LIMIT       0x4812
#define VMCS_GUEST_ES_ACCESS_RIGHTS 0x4814
#define VMCS_GUEST_CS_ACCESS_RIGHTS 0x4816
#define VMCS_GUEST_SS_ACCESS_RIGHTS 0x4818
#define VMCS_GUEST_DS_ACCESS_RIGHTS 0x481A
#define VMCS_GUEST_FS_ACCESS_RIGHTS 0x481C
#define VMCS_GUEST_GS_ACCESS_RIGHTS 0x481E
#define VMCS_GUEST_LDTR_ACCESS_RIGHTS 0x4820
#define VMCS_GUEST_TR_ACCESS_RIGHTS 0x4822
#define VMCS_GUEST_INTERRUPTIBILITY 0x4824
#define VMCS_GUEST_ACTIVITY_STATE   0x4826
#define VMCS_GUEST_SYSENTER_CS      0x482A

#define VMCS_GUEST_CR0              0x6800
#define VMCS_GUEST_CR3              0x6802
#define VMCS_GUEST_CR4              0x6804
#define VMCS_GUEST_ES_BASE          0x6806
#define VMCS_GUEST_CS_BASE          0x6808
#define VMCS_GUEST_SS_BASE          0x680A
#define VMCS_GUEST_DS_BASE          0x680C
#define VMCS_GUEST_FS_BASE          0x680E
#define VMCS_GUEST_GS_BASE          0x6810
#define VMCS_GUEST_LDTR_BASE        0x6812
#define VMCS_GUEST_TR_BASE          0x6814
#define VMCS_GUEST_GDTR_BASE        0x6816
#define VMCS_GUEST_IDTR_BASE        0x6818
#define VMCS_GUEST_DR7              0x681A
#define VMCS_GUEST_RSP              0x681C
#define VMCS_GUEST_RIP              0x681E
#define VMCS_GUEST_RFLAGS           0x6820
#define VMCS_GUEST_SYSENTER_ESP     0x6824
#define VMCS_GUEST_SYSENTER_EIP     0x6826

#define VMCS_HOST_CR0               0x6C00
#define VMCS_HOST_CR3               0x6C02
#define VMCS_HOST_CR4               0x6C04
#define VMCS_HOST_FS_BASE           0x6C06
#define VMCS_HOST_GS_BASE           0x6C08
#define VMCS_HOST_TR_BASE           0x6C0A
#define VMCS_HOST_GDTR_BASE         0x6C0C
#define VMCS_HOST_IDTR_BASE         0x6C0E
#define VMCS_HOST_SYSENTER_ESP      0x6C10
#define VMCS_HOST_SYSENTER_EIP      0x6C12
#define VMCS_HOST_RSP               0x6C14
#define VMCS_HOST_RIP               0x6C16

#define VMCS_IO_BITMAP_A            0x2000
#define VMCS_IO_BITMAP_B            0x2002
#define VMCS_EPT_POINTER            0x201A
#define VMCS_LINK_POINTER           0x2800

#define VMCS_GUEST_VMCS_LINK_PTR    0x2800
#define VMCS_GUEST_IA32_EFER        0x2806

#define VMCS_HOST_IA32_EFER         0x2C02

#define PIN_BASED_EXT_INT_EXIT      (1U << 0)
#define PIN_BASED_NMI_EXIT          (1U << 3)

#define PROC_BASED_HLT_EXIT         (1U << 7)
#define PROC_BASED_INVLPG_EXIT      (1U << 9)
#define PROC_BASED_MWAIT_EXIT       (1U << 10)
#define PROC_BASED_RDPMC_EXIT       (1U << 11)
#define PROC_BASED_UNCOND_IO_EXIT   (1U << 24)
#define PROC_BASED_USE_IO_BITMAPS   (1U << 25)
#define PROC_BASED_USE_MSR_BITMAPS  (1U << 28)
#define PROC_BASED_MONITOR_EXIT     (1U << 29)
#define PROC_BASED_ACTIVATE_SECONDARY (1U << 31)

#define PROC2_BASED_EPT             (1U << 1)
#define PROC2_BASED_UNRESTRICTED    (1U << 7)

#define EXIT_CTRL_HOST_ADDR_SPACE_SIZE (1U << 9)
#define EXIT_CTRL_ACK_INT_ON_EXIT     (1U << 15)

#define ENTRY_CTRL_IA32E_MODE_GUEST   (1U << 9)

#define VMX_EXIT_REASON_EXCEPTION_NMI  0
#define VMX_EXIT_REASON_EXT_INT        1
#define VMX_EXIT_REASON_TRIPLE_FAULT   2
#define VMX_EXIT_REASON_CPUID          10
#define VMX_EXIT_REASON_HLT            12
#define VMX_EXIT_REASON_INVLPG         14
#define VMX_EXIT_REASON_RDPMC          15
#define VMX_EXIT_REASON_VMCALL         18
#define VMX_EXIT_REASON_CR_ACCESS      28
#define VMX_EXIT_REASON_IO_INSTRUCTION 30
#define VMX_EXIT_REASON_MSR_READ       31
#define VMX_EXIT_REASON_MSR_WRITE      32
#define VMX_EXIT_REASON_EPT_VIOLATION  48
#define VMX_EXIT_REASON_EPT_MISCONFIG  49
#define VMX_EXIT_REASON_XSETBV         55

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cr0, cr3, cr4;
    uint16_t cs, ds, es, fs, gs, ss;
    uint16_t tr, ldtr;
    uint32_t _pad;
    uint32_t cs_base;
} vmx_regs_t;

typedef struct {
    uint32_t exit_reason;
    uint32_t _pad0;
    uint64_t exit_qualification;
    uint16_t io_port;
    uint8_t  io_size;
    uint8_t  io_direction;
    uint32_t io_data;
    uint64_t guest_phys_addr;
    uint64_t guest_linear_addr;
    uint64_t guest_rip;
    uint32_t instruction_length;
    uint32_t _pad1;
} vmx_exit_info_t;

typedef struct {
    uint64_t guest_phys_addr;
    uint64_t host_virt_addr;
    uint64_t size;
    uint32_t flags;
    uint32_t _pad;
} vmx_memory_region_t;

typedef struct {
    uint64_t guest_phys_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} vmx_framebuffer_t;

#define VMX_MAX_VMS          4
#define VMX_MAX_MEM_REGIONS  16
#define VMX_GUEST_RAM_MAX    (256ULL * 1024ULL * 1024ULL)

struct vmx_ept;

typedef struct {
    uint8_t  active;
    uint8_t  launched;
    uint8_t  _pad[6];

    void    *vmxon_region;

    void    *vmcs_region;
    uint64_t vmcs_phys;

    vmx_regs_t guest_regs;

    struct vmx_ept *ept;

    vmx_memory_region_t mem_regions[VMX_MAX_MEM_REGIONS];
    uint32_t mem_region_count;

    vmx_framebuffer_t guest_fb;
    uint8_t guest_fb_valid;

    vmx_exit_info_t last_exit;
} vmx_vm_t;

int vtx_init(void);
int vtx_is_available(void);
int32_t vtx_vm_create(void);
void vtx_vm_destroy(int32_t vm_id);
int vtx_vm_set_regs(int32_t vm_id, const vmx_regs_t *regs);
int vtx_vm_get_regs(int32_t vm_id, vmx_regs_t *regs);
int vtx_vm_map_memory(int32_t vm_id, const vmx_memory_region_t *region);
int vtx_vm_set_framebuffer(int32_t vm_id, const vmx_framebuffer_t *fb);
int vtx_vm_run(int32_t vm_id);
int vtx_vm_get_exit_info(int32_t vm_id, vmx_exit_info_t *info);
int vtx_vm_set_io_response(int32_t vm_id, uint32_t data);

uint64_t vtx_adjust_cr0(uint64_t cr0);
uint64_t vtx_adjust_cr4(uint64_t cr4);

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

static inline uint64_t read_cr0(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline void write_cr0(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr0" :: "r"(v) : "memory");
}

static inline uint64_t read_cr3_vtx(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline uint64_t read_cr4(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

static inline void write_cr4(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr4" :: "r"(v) : "memory");
}

static inline int vmx_vmxon(uint64_t *vmxon_region_phys)
{
    uint8_t err;
    __asm__ volatile("vmxon %[addr]; setna %[err]"
                     : [err] "=rm"(err)
                     : [addr] "m"(*vmxon_region_phys)
                     : "cc", "memory");
    return err ? -1 : 0;
}

static inline int vmx_vmclear(uint64_t *vmcs_phys)
{
    uint8_t err;
    __asm__ volatile("vmclear %[addr]; setna %[err]"
                     : [err] "=rm"(err)
                     : [addr] "m"(*vmcs_phys)
                     : "cc", "memory");
    return err ? -1 : 0;
}

static inline int vmx_vmptrld(uint64_t *vmcs_phys)
{
    uint8_t err;
    __asm__ volatile("vmptrld %[addr]; setna %[err]"
                     : [err] "=rm"(err)
                     : [addr] "m"(*vmcs_phys)
                     : "cc", "memory");
    return err ? -1 : 0;
}

static inline int vmx_vmwrite(uint64_t field, uint64_t value)
{
    uint8_t err;
    __asm__ volatile("vmwrite %[val], %[field]; setna %[err]"
                     : [err] "=rm"(err)
                     : [field] "r"(field), [val] "rm"(value)
                     : "cc");
    return err ? -1 : 0;
}

static inline int vmx_vmread(uint64_t field, uint64_t *value)
{
    uint8_t err;
    __asm__ volatile("vmread %[field], %[val]; setna %[err]"
                     : [val] "=rm"(*value), [err] "=rm"(err)
                     : [field] "r"(field)
                     : "cc");
    return err ? -1 : 0;
}

static inline int vmx_invept(uint64_t type, uint64_t eptp)
{
    struct {
        uint64_t eptp;
        uint64_t reserved;
    } descriptor = {eptp, 0};
    uint8_t err;
    __asm__ volatile("invept %[desc], %[type]; setna %[err]"
                     : [err] "=rm"(err)
                     : [desc] "m"(descriptor), [type] "r"(type)
                     : "cc", "memory");
    return err ? -1 : 0;
}