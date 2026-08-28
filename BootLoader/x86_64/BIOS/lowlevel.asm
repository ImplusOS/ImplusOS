BITS 32

%include "BootManager/BIOS/stage2_constants.inc"

section .text
global bios_read_sector32
global bios_enter_kernel64

bios_read_sector32:
    mov [pm_saved_esp], esp
    mov [pm_saved_ebx], ebx
    mov [pm_saved_esi], esi
    mov [pm_saved_edi], edi
    mov [pm_saved_ebp], ebp
    mov eax, [esp + 4]
    mov [rm_drive], al
    mov eax, [esp + 8]
    mov [rm_dap + 8], eax
    mov eax, [esp + 12]
    mov [rm_dap + 12], eax
    mov eax, [esp + 16]
    mov [rm_dest], eax

    cli
    jmp CODE16_SEL:protected16_to_real

BITS 16
protected16_to_real:
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax
    jmp 0x0000:real_mode_read

real_mode_read:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6F00

    mov byte [rm_status], 1
    mov si, rm_dap
    mov ah, 0x42
    mov dl, [rm_drive]
    int 0x13
    jc .done
    mov byte [rm_status], 0
.done:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE32_SEL:protected_read_return

BITS 32
protected_read_return:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [pm_saved_esp]

    cmp byte [rm_status], 0
    jne .error
    mov esi, 0x7C00
    mov edi, [rm_dest]
    mov ecx, 512 / 4
    rep movsd
    mov ebx, [pm_saved_ebx]
    mov esi, [pm_saved_esi]
    mov edi, [pm_saved_edi]
    mov ebp, [pm_saved_ebp]
    xor eax, eax
    ret
.error:
    mov ebx, [pm_saved_ebx]
    mov esi, [pm_saved_esi]
    mov edi, [pm_saved_edi]
    mov ebp, [pm_saved_ebp]
    mov eax, -1
    ret

bios_enter_kernel64:
    mov eax, [esp + 4]
    mov [kernel_entry32], eax
    mov eax, [esp + 8]
    mov [kernel_boot_info32], eax

    call setup_long_mode_tables

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, pml4_table
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    jmp CODE64_SEL:long_mode_start

setup_long_mode_tables:
    mov edi, pml4_table
    mov ecx, 4096 * 6 / 4
    xor eax, eax
    rep stosd

    mov eax, pdpt_table
    or eax, 0x003
    mov [pml4_table], eax
    mov dword [pml4_table + 4], 0

    mov esi, pd_tables
    xor ecx, ecx
.pdpt_loop:
    mov eax, esi
    or eax, 0x003
    mov [pdpt_table + ecx * 8], eax
    mov dword [pdpt_table + ecx * 8 + 4], 0

    xor edx, edx
.pd_loop:
    mov eax, ecx
    shl eax, 30
    mov ebx, edx
    shl ebx, 21
    add eax, ebx
    or eax, 0x083
    mov [esi + edx * 8], eax
    mov dword [esi + edx * 8 + 4], 0
    inc edx
    cmp edx, 512
    jne .pd_loop

    add esi, 4096
    inc ecx
    cmp ecx, 4
    jne .pdpt_loop
    ret

BITS 64
long_mode_start:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x90000
    mov rdi, qword [rel kernel_boot_info32]
    mov rax, qword [rel kernel_entry32]
    jmp rax

BITS 32
section .data
align 8
global gdt_start
global gdt_end
global gdt_descriptor

gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00009A000000FFFF
    dq 0x00AF9A000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

global pm_saved_esp
global pm_saved_ebx
global pm_saved_esi
global pm_saved_edi
global pm_saved_ebp
global rm_dest
global rm_drive
global rm_status
global rm_dap
global kernel_entry32
global kernel_boot_info32

kernel_entry32: dq 0
kernel_boot_info32: dq 0
pm_saved_esp: dd 0
pm_saved_ebx: dd 0
pm_saved_esi: dd 0
pm_saved_edi: dd 0
pm_saved_ebp: dd 0
rm_dest: dd 0
rm_drive: db 0
rm_status: db 0
rm_dap:
    db 0x10
    db 0
    dw 1
    dw 0x7C00
    dw 0
    dq 0

section .bss
alignb 4096
pml4_table: resb 4096
pdpt_table: resb 4096
pd_tables: resb 4096 * 4
