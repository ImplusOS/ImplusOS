BITS 16
ORG 0x7C00

%define STAGE2_SEG      0x0000
%define STAGE2_OFF      0x8000
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS  96
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov si, dap
    mov word [si + 2], STAGE2_SECTORS
    mov word [si + 4], STAGE2_OFF
    mov word [si + 6], STAGE2_SEG
    mov dword [si + 8], 1
    mov dword [si + 12], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc boot_error

    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_OFF

boot_error:
    mov si, error_msg
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

boot_drive: db 0
error_msg: db "ImplusOS BIOS stage1: disk read failed", 13, 10, 0

dap:
    db 0x10
    db 0
    dw 0
    dw 0
    dw 0
    dq 0

times 510 - ($ - $$) db 0
dw 0xAA55
