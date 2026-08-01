BITS 64
section .text
global syscall_entry
global syscall_enter_user_from_frame
extern syscall_dispatch

syscall_entry:
    swapgs
    
    mov [gs:0], rsp
    mov rsp, [gs:8]
    
    push r11
    push rcx
    push rbp
    push rbx
    push r15
    push r14
    push r13
    push r12
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rax
    
    mov rdi, rsp
    mov rsi, rax
    mov rdx, [rsp + 24]
    mov rcx, [rsp + 16]
    
    mov rax, [rsp + 32]
    
    mov r8, [rsp + 8]
    mov r9, [rsp + 48]

    push rax
    
    mov rax, [rsp + 48]
    push rax
    
    sub rsp, 8
    
    call syscall_dispatch
    
    mov rsp, rax
    
    pop rax
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbx
    pop rbp
    pop rcx
    pop r11

    mov rsp, [gs:0]
    
    swapgs
    o64 sysret

syscall_enter_user_from_frame:
    cli
    cld
    mov rbx, rdi

    mov rax, [rbx + (13 * 8)]
    mov rdx, [rbx + (14 * 8)]

    push qword (0x20 | 3)
    push rsi
    push rdx
    push qword (0x28 | 3)
    push rax

    mov rax, [rbx + (0 * 8)]
    mov rdx, [rbx + (1 * 8)]
    mov rsi, [rbx + (2 * 8)]
    mov rdi, [rbx + (3 * 8)]
    mov r8,  [rbx + (4 * 8)]
    mov r9,  [rbx + (5 * 8)]
    mov r10, [rbx + (6 * 8)]
    mov r12, [rbx + (7 * 8)]
    mov r13, [rbx + (8 * 8)]
    mov r14, [rbx + (9 * 8)]
    mov r15, [rbx + (10 * 8)]
    mov rbp, [rbx + (12 * 8)]
    mov rcx, [rbx + (13 * 8)]
    mov r11, [rbx + (14 * 8)]
    mov rbx, [rbx + (11 * 8)]
    
    swapgs

    iretq

section .note.GNU-stack noalloc noexec nowrite progbits