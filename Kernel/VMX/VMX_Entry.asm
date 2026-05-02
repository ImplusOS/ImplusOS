BITS 64
section .text

global vmx_vmlaunch_asm
global vmx_vmresume_asm

;; ──────────────────────────────────────────────────────────────
;; int vmx_vmlaunch_asm(vmx_regs_t *guest_regs)
;; int vmx_vmresume_asm(vmx_regs_t *guest_regs)
;;
;; guest_regs layout (vmx_regs_t):
;;   offset  0: rax     offset  8: rbx
;;   offset 16: rcx     offset 24: rdx
;;   offset 32: rsi     offset 40: rdi
;;   offset 48: rbp     offset 56: rsp  (not used for guest RSP in VMCS)
;;   offset 64: r8      offset 72: r9
;;   offset 80: r10     offset 88: r11
;;   offset 96: r12     offset104: r13
;;   offset112: r14     offset120: r15
;;   offset128: rip     offset136: rflags (not loaded from here)
;;
;; Returns: 0 on VM Exit (success), 1 on VMfailInvalid, 2 on VMfailValid
;; ──────────────────────────────────────────────────────────────

;; Save host callee-saved regs, load guest regs, VMLAUNCH, then
;; on VM Exit capture guest regs, restore host regs, return.

vmx_vmlaunch_asm:
    pushfq
    cli

    ;; Save host callee-saved registers
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    push rdi            ; save guest_regs pointer

    ;; Write current RSP to VMCS_HOST_RSP (0x6C14)
    ;; VMWRITE is vmwrite <value>, <field>.
    mov rax, rsp
    mov rdx, 0x6C14
    vmwrite rax, rdx

    ;; Load guest general-purpose regs from vmx_regs_t
    mov rax, [rdi +   0]
    mov rbx, [rdi +   8]
    mov rcx, [rdi +  16]
    mov rdx, [rdi +  24]
    mov rsi, [rdi +  32]
    mov rbp, [rdi +  48]
    mov r8,  [rdi +  64]
    mov r9,  [rdi +  72]
    mov r10, [rdi +  80]
    mov r11, [rdi +  88]
    mov r12, [rdi +  96]
    mov r13, [rdi + 104]
    mov r14, [rdi + 112]
    mov r15, [rdi + 120]
    ;; Load rdi last (it holds the pointer)
    mov rdi, [rdi +  40]

    vmlaunch

    ;; If we reach here, VMLAUNCH failed
    jmp vm_fail

vmx_vmresume_asm:
    pushfq
    cli

    ;; Save host callee-saved registers
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    push rdi            ; save guest_regs pointer

    ;; Write current RSP to VMCS_HOST_RSP (0x6C14)
    ;; VMWRITE is vmwrite <value>, <field>.
    mov rax, rsp
    mov rdx, 0x6C14
    vmwrite rax, rdx

    ;; Load guest general-purpose regs from vmx_regs_t
    mov rax, [rdi +   0]
    mov rbx, [rdi +   8]
    mov rcx, [rdi +  16]
    mov rdx, [rdi +  24]
    mov rsi, [rdi +  32]
    mov rbp, [rdi +  48]
    mov r8,  [rdi +  64]
    mov r9,  [rdi +  72]
    mov r10, [rdi +  80]
    mov r11, [rdi +  88]
    mov r12, [rdi +  96]
    mov r13, [rdi + 104]
    mov r14, [rdi + 112]
    mov r15, [rdi + 120]
    mov rdi, [rdi +  40]

    vmresume

    ;; If we reach here, VMRESUME failed
    jmp vm_fail

;; ── VM Exit landing point ─────────────────────────────────────
;; The VMCS Host RIP is set to this label.
global vmx_vmexit_handler
vmx_vmexit_handler:
    ;; We just exited the guest. The host RSP was restored by hardware
    ;; (it was set to point to the stacked guest_regs pointer).
    ;; The host callee-saved regs + guest_regs ptr are on the stack.

    ;; Recover guest_regs pointer (was pushed last before guest regs load)
    ;; Host RSP was set to point just above the pushed rdi
    ;; Stack: [r15][r14][r13][r12][rbp][rbx][rdi=guest_regs_ptr]
    ;; Host RSP → top of this frame

    ;; We need to save guest regs first. Push rdi (guest value) temporarily.
    push rdi

    ;; Get guest_regs pointer from stack (it's at RSP + 8, was the top before push)
    mov rdi, [rsp + 8]

    ;; Save guest general-purpose regs
    mov [rdi +   0], rax
    mov [rdi +   8], rbx
    mov [rdi +  16], rcx
    mov [rdi +  24], rdx
    mov [rdi +  32], rsi
    ;; rdi (guest) was pushed, get it from stack
    pop rax             ; rax = guest rdi
    mov [rdi +  40], rax
    mov [rdi +  48], rbp
    ;; guest rsp is in VMCS, not in a GPR
    mov [rdi +  64], r8
    mov [rdi +  72], r9
    mov [rdi +  80], r10
    mov [rdi +  88], r11
    mov [rdi +  96], r12
    mov [rdi + 104], r13
    mov [rdi + 112], r14
    mov [rdi + 120], r15

    ;; Pop the saved guest_regs pointer (still on stack)
    pop rdi

    ;; Restore host callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq

    ;; Return 0 (success — VM Exit occurred)
    xor eax, eax
    ret

vm_fail:
    ;; VMLAUNCH/VMRESUME failed
    ;; Pop the saved guest_regs pointer
    pop rdi

    ;; Restore host callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    popfq

    ;; Check CF (VMfailInvalid) vs ZF (VMfailValid)
    jc vm_fail_invalid
    ;; VMfailValid
    mov eax, 2
    ret

vm_fail_invalid:
    mov eax, 1
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
