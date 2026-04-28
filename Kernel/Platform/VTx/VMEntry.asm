BITS 64
section .text

global vmx_run_guest
global vmx_vmexit_entry
extern vtx_handle_vmexit

;; ──────────────────────────────────────────────────────────────
;; int vmx_run_guest(vmx_regs_t *guest_regs, int launched)
;;   rdi = pointer to vmx_regs_t
;;   esi = 0 → vmlaunch, non-zero → vmresume
;;
;; VMCS HOST_RIP must already point to vmx_vmexit_entry (set by C).
;; This function writes HOST_RSP = current RSP via vmwrite.
;; Returns 0 on VM exit, -1 on vmlaunch/vmresume failure.
;; ──────────────────────────────────────────────────────────────

vmx_run_guest:
    ;; Save host callee-saved registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq

    ;; Save guest_regs pointer to global (absolute 64-bit address)
    mov rax, g_guest_regs_ptr
    mov [rax], rdi

    ;; vmwrite HOST_RSP = current RSP (after all pushes)
    mov rcx, 0x6C14
    vmwrite rcx, rsp
    jc .vmfail
    jz .vmfail

    ;; Test launched flag BEFORE loading guest regs.
    ;; mov instructions do NOT modify flags, so the test result
    ;; persists through all the guest register loads below.
    test esi, esi

    ;; Load guest GPRs from vmx_regs_t struct
    mov rax, [rdi + 0]
    mov rbx, [rdi + 8]
    mov rcx, [rdi + 16]
    mov rdx, [rdi + 24]
    mov rsi, [rdi + 32]
    mov rbp, [rdi + 48]
    mov r8,  [rdi + 64]
    mov r9,  [rdi + 72]
    mov r10, [rdi + 80]
    mov r11, [rdi + 88]
    mov r12, [rdi + 96]
    mov r13, [rdi + 104]
    mov r14, [rdi + 112]
    mov r15, [rdi + 120]
    ;; Load guest rdi last (clobbers struct pointer)
    mov rdi, [rdi + 40]

    ;; Branch based on test result above
    jnz .do_resume

    vmlaunch
    jmp .vmfail

.do_resume:
    vmresume
    ;; Fall through to .vmfail if vmresume fails

.vmfail:
    ;; vmlaunch/vmresume failed. RSP is still our host stack.
    ;; GPRs contain guest values but that's OK, we just restore host state.
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    mov eax, -1
    ret

;; ──────────────────────────────────────────────────────────────
;; vmx_vmexit_entry — VM Exit landing point
;;
;; The CPU restored HOST_RSP (our host stack with saved callee regs)
;; and jumped here. All GPRs hold guest values.
;;
;; Strategy: push guest rax to free up a scratch register, then
;; use rax to load the guest_regs pointer and save everything.
;; ──────────────────────────────────────────────────────────────

vmx_vmexit_entry:
    ;; Save guest rax on the host stack (HOST_RSP was restored by CPU)
    push rax

    ;; Load guest_regs pointer using absolute addressing
    mov rax, g_guest_regs_ptr
    mov rax, [rax]

    ;; Save all guest GPRs into vmx_regs_t
    mov [rax + 8],   rbx
    mov [rax + 16],  rcx
    mov [rax + 24],  rdx
    mov [rax + 32],  rsi
    mov [rax + 40],  rdi
    mov [rax + 48],  rbp
    mov [rax + 64],  r8
    mov [rax + 72],  r9
    mov [rax + 80],  r10
    mov [rax + 88],  r11
    mov [rax + 96],  r12
    mov [rax + 104], r13
    mov [rax + 112], r14
    mov [rax + 120], r15

    ;; Recover guest rax from stack and save it
    mov rdi, rax          ; rdi = guest_regs pointer (also serves as arg for C call)
    pop rax               ; rax = original guest rax
    mov [rdi + 0], rax

    ;; Call C handler: vtx_handle_vmexit(vmx_regs_t *guest_regs)
    call vtx_handle_vmexit

    ;; Restore host callee-saved registers (pushed by vmx_run_guest)
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ;; Return 0 = successful VM exit
    xor eax, eax
    ret

section .data
g_guest_regs_ptr:  dq 0

section .note.GNU-stack noalloc noexec nowrite progbits
