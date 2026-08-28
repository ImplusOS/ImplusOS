BITS 16

%include "BootManager/BIOS/stage2_constants.inc"

section .text.stage2
global bios_stage2_start
extern bootmanager_bios_main

bios_stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti

    mov [boot_drive], dl
    call enable_a20
    call collect_e820
    call setup_vbe
    call find_acpi_rsdp

    cli
    extern gdt_descriptor
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE32_SEL:protected_start

enable_a20:
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

collect_e820:
    pushad
    xor ebx, ebx
    xor bp, bp
.e820_next:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    mov di, BIOS_MEMORY_MAP_ADDRESS
    add di, bp
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    test ecx, ecx
    jz .e820_skip
    add bp, 24
    inc dword [e820_count]
.e820_skip:
    test ebx, ebx
    jnz .e820_next
.e820_done:
    popad
    ret

setup_vbe:
    push es
    xor ax, ax
    mov es, ax

    mov di, vbe_mode_info
    mov ax, 0x4F01
    mov cx, 0x143
    int 0x10
    cmp ax, 0x004F
    jne .try_24bpp
    cmp byte [vbe_mode_info + 25], 32
    jne .try_24bpp
    mov ax, 0x4F02
    mov bx, 0x4143
    int 0x10
    cmp ax, 0x004F
    jne .try_24bpp
    mov word [vbe_selected_mode], 0x143
    jmp .done

.try_24bpp:
    mov di, vbe_mode_info
    mov ax, 0x4F01
    mov cx, 0x118
    int 0x10
    cmp ax, 0x004F
    jne .done
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .done
    mov word [vbe_selected_mode], 0x118

.done:
    pop es
    ret

find_acpi_rsdp:
    ret

BITS 32
protected_start:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    call build_boot_params
    push dword BIOS_BOOT_INFO_ADDRESS
    call bootmanager_bios_main
.halt:
    cli
    hlt
    jmp .halt

build_boot_params:
    mov edi, BIOS_BOOT_INFO_ADDRESS
    mov ecx, 128
    xor eax, eax
    rep stosd

    mov dword [BIOS_BOOT_INFO_ADDRESS + 0], BIOS_BOOT_PARAMS_SIGNATURE
    mov dword [BIOS_BOOT_INFO_ADDRESS + 4], 1
    mov al, [boot_drive]
    mov [BIOS_BOOT_INFO_ADDRESS + 8], al

    mov eax, [vbe_mode_info + 40]
    mov [BIOS_BOOT_INFO_ADDRESS + 24], eax
    
    movzx eax, word [vbe_mode_info + 18]
    mov [BIOS_BOOT_INFO_ADDRESS + 36], eax

    movzx eax, word [vbe_mode_info + 20]
    mov [BIOS_BOOT_INFO_ADDRESS + 40], eax

    movzx eax, word [vbe_mode_info + 16]
    movzx ecx, byte [vbe_mode_info + 25]
    shr ecx, 3
    test ecx, ecx
    jz .ppl_skip
    xor edx, edx
    div ecx
.ppl_skip:
    mov [BIOS_BOOT_INFO_ADDRESS + 44], eax

    movzx eax, word [vbe_selected_mode]
    mov [BIOS_BOOT_INFO_ADDRESS + 48], eax

    movzx eax, word [vbe_mode_info + 20]
    movzx ebx, word [vbe_mode_info + 16]
    mul ebx
    mov [BIOS_BOOT_INFO_ADDRESS + 32], eax

    mov dword [BIOS_BOOT_INFO_ADDRESS + 52], BIOS_MEMORY_MAP_ADDRESS
    mov eax, [e820_count]
    mov [BIOS_BOOT_INFO_ADDRESS + 60], eax

    mov dword [BIOS_BOOT_INFO_ADDRESS + 64], 24

    mov eax, [acpi_rsdp_ptr]
    mov [BIOS_BOOT_INFO_ADDRESS + 68], eax
    mov dword [BIOS_BOOT_INFO_ADDRESS + 72], 0

    extern bios_read_sector32
    mov dword [BIOS_BOOT_INFO_ADDRESS + 76], bios_read_sector32
    extern bios_enter_kernel64
    mov dword [BIOS_BOOT_INFO_ADDRESS + 80], bios_enter_kernel64
    ret

section .data
global boot_drive
global e820_count
global acpi_rsdp_ptr

boot_drive: db 0
e820_count: dd 0
acpi_rsdp_ptr: dd 0

section .bss
global vbe_mode_info
global vbe_selected_mode

alignb 16
vbe_mode_info:      resb 256
vbe_selected_mode:  resw 1
