#include "VTx.h"
#include "EPT.h"
#include "../../Debbuger/printf/printf.h"
#include "../../Debbuger/Serial/Serial.h"

#include <stddef.h>
#include <stdint.h>

extern vmx_vm_t *vtx_get_current_vm(void);

void vtx_handle_vmexit(vmx_regs_t *guest_regs)
{
    vmx_vm_t *vm = vtx_get_current_vm();
    if (vm == NULL) {
        return;
    }

    uint64_t guest_rsp = 0;
    vmx_vmread(VMCS_GUEST_RSP, &guest_rsp);
    guest_regs->rsp = guest_rsp;

    uint64_t guest_rip = 0;
    vmx_vmread(VMCS_GUEST_RIP, &guest_rip);
    guest_regs->rip = guest_rip;

    uint64_t guest_rflags = 0;
    vmx_vmread(VMCS_GUEST_RFLAGS, &guest_rflags);
    guest_regs->rflags = guest_rflags;

    uint64_t exit_reason_raw = 0;
    vmx_vmread(VMCS_EXIT_REASON, &exit_reason_raw);
    uint32_t exit_reason = (uint32_t)(exit_reason_raw & 0xFFFF);

    uint64_t exit_qual = 0;
    vmx_vmread(VMCS_EXIT_QUALIFICATION, &exit_qual);

    uint64_t instr_len = 0;
    vmx_vmread(VMCS_EXIT_INSTRUCTION_LEN, &instr_len);

    vmx_exit_info_t *info = &vm->last_exit;
    info->exit_reason = exit_reason;
    info->exit_qualification = exit_qual;
    info->guest_rip = guest_rip;
    info->instruction_length = (uint32_t)instr_len;
    info->io_port = 0;
    info->io_size = 0;
    info->io_direction = 0;
    info->io_data = 0;
    info->guest_phys_addr = 0;
    info->guest_linear_addr = 0;

    if (exit_reason_raw & 0x80000000ULL) {
        vm->launched = 0;
        return;
    }

    switch (exit_reason) {
        case VMX_EXIT_REASON_IO_INSTRUCTION: {
            info->io_port = (uint16_t)(exit_qual >> 16);
            info->io_direction = (uint8_t)((exit_qual >> 3) & 1);
            uint8_t size_enc = (uint8_t)(exit_qual & 0x7);
            info->io_size = (uint8_t)(size_enc + 1);

            if (info->io_direction == 0) {
                info->io_data = (uint32_t)guest_regs->rax;
            }

            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            break;
        }

        case VMX_EXIT_REASON_EPT_VIOLATION: {
            uint64_t gpa = 0;
            vmx_vmread(0x2400, &gpa);
            info->guest_phys_addr = gpa;

            uint64_t gla = 0;
            vmx_vmread(0x640A, &gla);
            info->guest_linear_addr = gla;
            break;
        }

        case VMX_EXIT_REASON_CPUID: {
            uint32_t leaf = (uint32_t)guest_regs->rax;
            uint32_t subleaf = (uint32_t)guest_regs->rcx;
            (void)subleaf;

            switch (leaf) {
                case 0x0:
                    guest_regs->rax = 0x0D;
                    guest_regs->rbx = 0x6C706D49;  
                    guest_regs->rdx = 0x564F7375;  
                    guest_regs->rcx = 0x0000004D;  
                    break;
                case 0x1:
                    guest_regs->rax = 0x000306C3;
                    guest_regs->rbx = 0x00010800;
                    guest_regs->rcx = 0x80202201;
                    guest_regs->rdx = 0x178BFBFF;
                    break;
                case 0x2:
                    guest_regs->rax = 0x00000001;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
                case 0x4:
                    guest_regs->rax = 0;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
                case 0x7:
                    guest_regs->rax = 0;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
                case 0xB:
                    guest_regs->rax = 0;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = subleaf;
                    guest_regs->rdx = 0;
                    break;
                case 0xD:
                    guest_regs->rax = 0;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
                case 0x80000000:
                    guest_regs->rax = 0x80000008;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
                case 0x80000001:
                    guest_regs->rax = 0;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = (1U << 0);
                    guest_regs->rdx = (1U << 29) |
                                      (1U << 27) |
                                      (1U << 26) |
                                      (1U << 20) |
                                      (1U << 11);
                    break;
                case 0x80000002:
                    guest_regs->rax = 0x6C706D49;
                    guest_regs->rbx = 0x564F7375;
                    guest_regs->rcx = 0x7269564D;
                    guest_regs->rdx = 0x6C617574;
                    break;
                case 0x80000003:
                    guest_regs->rax = 0x55504320;
                    guest_regs->rbx = 0x00000000;
                    guest_regs->rcx = 0x00000000;
                    guest_regs->rdx = 0x00000000;
                    break;
                case 0x80000004:
                    guest_regs->rax = 0x00000000;
                    guest_regs->rbx = 0x00000000;
                    guest_regs->rcx = 0x00000000;
                    guest_regs->rdx = 0x00000000;
                    break;
                case 0x80000008:
                    guest_regs->rax = 0x00003027;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
                default:
                    guest_regs->rax = 0;
                    guest_regs->rbx = 0;
                    guest_regs->rcx = 0;
                    guest_regs->rdx = 0;
                    break;
            }

            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            info->exit_reason = VMX_EXIT_REASON_CPUID;
            break;
        }

        case VMX_EXIT_REASON_MSR_READ: {
            uint32_t msr_num = (uint32_t)guest_regs->rcx;
            uint64_t msr_val = 0;

            switch (msr_num) {
                case 0xC0000080: {
                    uint64_t guest_efer = 0;
                    vmx_vmread(VMCS_GUEST_IA32_EFER, &guest_efer);
                    msr_val = guest_efer;
                    break;
                }
                case 0x277: 
                    msr_val = 0x0007040600070406ULL;
                    break;
                case 0x1B:
                    msr_val = 0xFEE00900ULL;
                    break;
                case 0xFE:
                    msr_val = 0x0508;
                    break;
                case 0x2FF:
                    msr_val = 0x0C00;
                    break;
                case 0x10:
                    msr_val = 0;
                    break;
                default:
                    msr_val = 0;
                    break;
            }

            guest_regs->rax = msr_val & 0xFFFFFFFF;
            guest_regs->rdx = msr_val >> 32;
            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            break;
        }

        case VMX_EXIT_REASON_MSR_WRITE: {
            uint32_t msr_num = (uint32_t)guest_regs->rcx;
            uint64_t msr_val = (guest_regs->rax & 0xFFFFFFFF) |
                               (guest_regs->rdx << 32);

            switch (msr_num) {
                case 0xC0000080: {
                    vmx_vmwrite(VMCS_GUEST_IA32_EFER, msr_val);

                    uint64_t guest_cr0 = 0;
                    vmx_vmread(VMCS_GUEST_CR0, &guest_cr0);
                    int lme = (msr_val >> 8) & 1;
                    int pg  = (guest_cr0 >> 31) & 1;

                    uint64_t entry_ctrls = 0;
                    vmx_vmread(VMCS_ENTRY_CONTROLS, &entry_ctrls);
                    if (lme && pg) {
                        entry_ctrls |= ENTRY_CTRL_IA32E_MODE_GUEST;
                        msr_val |= (1ULL << 10);
                    } else {
                        entry_ctrls &= ~(uint64_t)ENTRY_CTRL_IA32E_MODE_GUEST;
                        msr_val &= ~(1ULL << 10);
                    }
                    vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctrls);
                    vmx_vmwrite(VMCS_GUEST_IA32_EFER, msr_val);
                    break;
                }
                default:
                    break;
            }

            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            break;
        }

        case VMX_EXIT_REASON_CR_ACCESS: {
            uint8_t cr_num = exit_qual & 0xF;
            uint8_t access_type = (exit_qual >> 4) & 0x3;
            uint8_t gpr = (exit_qual >> 8) & 0xF;
            
            uint64_t gpr_val = 0;
            uint64_t *gpr_ptr = NULL;
            switch (gpr) {
                case 0: gpr_ptr = &guest_regs->rax; break;
                case 1: gpr_ptr = &guest_regs->rcx; break;
                case 2: gpr_ptr = &guest_regs->rdx; break;
                case 3: gpr_ptr = &guest_regs->rbx; break;
                case 4: gpr_ptr = &guest_regs->rsp; break;
                case 5: gpr_ptr = &guest_regs->rbp; break;
                case 6: gpr_ptr = &guest_regs->rsi; break;
                case 7: gpr_ptr = &guest_regs->rdi; break;
                case 8: gpr_ptr = &guest_regs->r8; break;
                case 9: gpr_ptr = &guest_regs->r9; break;
                case 10: gpr_ptr = &guest_regs->r10; break;
                case 11: gpr_ptr = &guest_regs->r11; break;
                case 12: gpr_ptr = &guest_regs->r12; break;
                case 13: gpr_ptr = &guest_regs->r13; break;
                case 14: gpr_ptr = &guest_regs->r14; break;
                case 15: gpr_ptr = &guest_regs->r15; break;
            }
            if (gpr_ptr) gpr_val = *gpr_ptr;

            if (access_type == 0) {
                if (cr_num == 0) {
                    vmx_vmwrite(VMCS_GUEST_CR0, vtx_adjust_cr0(gpr_val));
                    vmx_vmwrite(0x6004, gpr_val);

                    uint64_t guest_efer = 0;
                    vmx_vmread(VMCS_GUEST_IA32_EFER, &guest_efer);
                    int lme = (guest_efer >> 8) & 1;
                    int pg  = (gpr_val >> 31) & 1;

                    uint64_t entry_ctrls = 0;
                    vmx_vmread(VMCS_ENTRY_CONTROLS, &entry_ctrls);
                    if (lme && pg) {
                        entry_ctrls |= ENTRY_CTRL_IA32E_MODE_GUEST;
                        guest_efer |= (1ULL << 10);
                    } else {
                        entry_ctrls &= ~(uint64_t)ENTRY_CTRL_IA32E_MODE_GUEST;
                        guest_efer &= ~(1ULL << 10);
                    }
                    vmx_vmwrite(VMCS_ENTRY_CONTROLS, entry_ctrls);
                    vmx_vmwrite(VMCS_GUEST_IA32_EFER, guest_efer);
                } else if (cr_num == 3) {
                    vmx_vmwrite(VMCS_GUEST_CR3, gpr_val);
                } else if (cr_num == 4) {
                    vmx_vmwrite(VMCS_GUEST_CR4, vtx_adjust_cr4(gpr_val));
                    vmx_vmwrite(0x6006, gpr_val);
                }
            } else if (access_type == 1 && gpr_ptr) {
                if (cr_num == 0) vmx_vmread(0x6004, gpr_ptr);
                else if (cr_num == 3) vmx_vmread(VMCS_GUEST_CR3, gpr_ptr);
                else if (cr_num == 4) vmx_vmread(0x6006, gpr_ptr);
            }

            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            break;
        }

        case VMX_EXIT_REASON_XSETBV: {
            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            break;
        }

        case VMX_EXIT_REASON_HLT:
            vmx_vmwrite(VMCS_GUEST_RIP, guest_rip + instr_len);
            break;

        case VMX_EXIT_REASON_EPT_MISCONFIG: {
            uint64_t gpa = 0;
            vmx_vmread(0x2400, &gpa);
            info->guest_phys_addr = gpa;
            break;
        }

        case VMX_EXIT_REASON_TRIPLE_FAULT:
        case VMX_EXIT_REASON_VMCALL:
        case VMX_EXIT_REASON_EXT_INT:
        case VMX_EXIT_REASON_EXCEPTION_NMI:
        default:
            break;
    }
}