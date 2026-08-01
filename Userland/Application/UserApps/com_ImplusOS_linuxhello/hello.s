BITS 64

section .text
global _start

; ------------------------------------------------------------------
; Linux ABI smoke test (ImplusOS)
; exercises: write, brk, getcwd, chdir, stat, open+getdents64,
;            prctl, mmap(MAP_FIXED), memfd_create, fstat,
;            timerfd_create/settime/gettime, nanosleep, exit_group
; ------------------------------------------------------------------

_start:
    ; T0: basic write
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel msg_t0]
    mov rdx, msg_t0_len
    syscall

    ; T1: brk
    mov rax, 12             ; brk(0)
    mov rdi, 0
    syscall
    mov [rel brk_old], rax
    add rax, 0x20000
    mov rdi, rax
    mov rax, 12             ; brk(old + 0x20000)
    syscall
    mov [rel brk_new], rax
    lea rsi, [rel msg_t1]
    mov rdx, msg_t1_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, [rel brk_new]
    call print_hex
    mov rax, [rel brk_old]
    add rax, 0x20000
    cmp [rel brk_new], rax
    jae t1_ok
    lea rsi, [rel msg_fail]
    mov rdx, msg_fail_len
    mov rax, 1
    mov rdi, 1
    syscall
    jmp t2
t1_ok:
    lea rsi, [rel msg_ok]
    mov rdx, msg_ok_len
    mov rax, 1
    mov rdi, 1
    syscall

t2: ; getcwd (79)
    mov rax, 79
    mov rdi, cwd_buf
    mov rsi, 256
    syscall
    cmp rax, -1
    je t2_fail
    lea rsi, [rel msg_t2]
    mov rdx, msg_t2_len
    mov rax, 1
    mov rdi, 1
    syscall
    lea rsi, [rel cwd_buf]
    mov rdx, 64
    mov rax, 1
    mov rdi, 1
    syscall
    jmp t3
t2_fail:
    lea rsi, [rel msg_t2_fail]
    mov rdx, msg_t2_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t3: ; chdir("/") then chdir("Userland") then getcwd (80)
    mov rax, 80
    mov rdi, dir_root
    syscall
    cmp rax, 0
    jne t3_fail
    mov rax, 80
    mov rdi, dir_userland
    syscall
    cmp rax, 0
    jne t3_fail
    mov rax, 79
    mov rdi, cwd_buf
    mov rsi, 256
    syscall
    lea rsi, [rel msg_t3]
    mov rdx, msg_t3_len
    mov rax, 1
    mov rdi, 1
    syscall
    lea rsi, [rel cwd_buf]
    mov rdx, 64
    mov rax, 1
    mov rdi, 1
    syscall
    jmp t4
t3_fail:
    lea rsi, [rel msg_t3_fail]
    mov rdx, msg_t3_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t4: ; stat("/") (4)
    mov rax, 4
    mov rdi, dir_root
    mov rsi, stat_buf
    syscall
    cmp rax, 0
    jne t4_fail
    lea rsi, [rel msg_t4]
    mov rdx, msg_t4_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov eax, [rel stat_buf + 24]   ; st_mode
    mov [rel stat_mode], eax
    call print_hex
    jmp t5
t4_fail:
    lea rsi, [rel msg_t4_fail]
    mov rdx, msg_t4_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t5: ; open("/") + getdents64 (2, 217)
    mov rax, 2
    mov rdi, dir_root
    mov rsi, 0x10000        ; O_RDONLY | O_DIRECTORY
    syscall
    cmp rax, 0
    jl t5_fail
    mov [rel dir_fd], rax
    mov rax, 217
    mov rdi, [rel dir_fd]
    mov rsi, dents_buf
    mov rdx, 4096
    syscall
    cmp rax, 0
    jle t5_fail
    mov [rel dents_len], rax
    lea rsi, [rel msg_t5]
    mov rdx, msg_t5_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, [rel dents_len]
    call print_hex
    ; print first entry name
    lea rsi, [rel dents_buf + 19]
    mov rdx, 32
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, 3
    mov rdi, [rel dir_fd]
    syscall
    jmp t6
t5_fail:
    lea rsi, [rel msg_t5_fail]
    mov rdx, msg_t5_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t6: ; prctl(PR_SET_NAME=15) + PR_GET_NAME (157)
    mov rax, 157
    mov rdi, 15
    mov rsi, name_set
    syscall
    cmp rax, 0
    jne t6_fail
    mov rax, 157
    mov rdi, 16
    mov rsi, name_get
    syscall
    cmp rax, 0
    jne t6_fail
    lea rsi, [rel msg_t6]
    mov rdx, msg_t6_len
    mov rax, 1
    mov rdi, 1
    syscall
    lea rsi, [rel name_get]
    mov rdx, 16
    mov rax, 1
    mov rdi, 1
    syscall
    jmp t7
t6_fail:
    lea rsi, [rel msg_t6_fail]
    mov rdx, msg_t6_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t7: ; mmap anonymous MAP_FIXED (9)
    mov rax, 9
    mov rdi, 0x100000000    ; addr
    mov rsi, 0x2000         ; length
    mov rdx, 3              ; PROT_READ|WRITE
    mov r10, 0x32           ; MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS
    mov r8, -1
    mov r9, 0
    syscall
    cmp rax, 0x100000000
    jne t7_fail
    mov rax, 0x1122334455667788
    mov [0x100000000], rax
    mov rax, [0x100000000]
    lea rsi, [rel msg_t7]
    mov rdx, msg_t7_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, [0x100000000]
    call print_hex
    jmp t8
t7_fail:
    lea rsi, [rel msg_t7_fail]
    mov rdx, msg_t7_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t8: ; memfd_create + write + lseek + read + fstat (319, 1, 8, 0, 5)
    mov rax, 319
    mov rdi, memfd_name
    mov rsi, 0
    syscall
    cmp rax, 0
    jl t8_fail
    mov [rel memfd], rax
    mov rax, 1
    mov rdi, [rel memfd]
    lea rsi, [rel memfd_data]
    mov rdx, 16
    syscall
    cmp rax, 16
    jne t8_fail
    mov rax, 8              ; lseek(fd, 0, SEEK_SET)
    mov rdi, [rel memfd]
    mov rsi, 0
    mov rdx, 0
    syscall
    mov rax, 0              ; read
    mov rdi, [rel memfd]
    lea rsi, [rel memfd_readback]
    mov rdx, 16
    syscall
    cmp rax, 16
    jne t8_fail
    mov rax, 5              ; fstat
    mov rdi, [rel memfd]
    mov rsi, stat_buf
    syscall
    cmp rax, 0
    jne t8_fail
    mov rax, [rel stat_buf + 48]   ; st_size
    lea rsi, [rel msg_t8]
    mov rdx, msg_t8_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, [rel stat_buf + 48]
    call print_hex
    mov rax, 3
    mov rdi, [rel memfd]
    syscall
    jmp t9
t8_fail:
    lea rsi, [rel msg_t8_fail]
    mov rdx, msg_t8_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t9: ; timerfd_create + settime(100ms) + nanosleep(150ms) + read (283, 286, 35, 0)
    mov rax, 283
    mov rdi, 0              ; CLOCK_MONOTONIC
    mov rsi, 0
    syscall
    cmp rax, 0
    jl t9_fail
    mov [rel tfd], rax
    mov qword [rel it_value + 0], 0    ; sec
    mov qword [rel it_value + 8], 100000000 ; nsec
    mov qword [rel it_interval + 0], 0
    mov qword [rel it_interval + 8], 100000000
    mov rax, 286
    mov rdi, [rel tfd]
    mov rsi, 0
    lea rdx, [rel it_value]
    lea r10, [rel it_interval]
    syscall
    cmp rax, 0
    jne t9_fail
    ; nanosleep 150ms
    mov qword [rel req_ts + 0], 0
    mov qword [rel req_ts + 8], 150000000
    mov rax, 35
    lea rdi, [rel req_ts]
    mov rsi, 0
    syscall
    ; read expirations
    mov rax, 0
    mov rdi, [rel tfd]
    lea rsi, [rel expirations]
    mov rdx, 8
    syscall
    cmp rax, 8
    jne t9_fail
    mov rax, [rel expirations]
    lea rsi, [rel msg_t9]
    mov rdx, msg_t9_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, [rel expirations]
    call print_hex
    ; timerfd_gettime (287)
    mov rax, 287
    mov rdi, [rel tfd]
    lea rsi, [rel it_value]
    lea rdx, [rel it_interval]
    syscall
    mov rax, 3
    mov rdi, [rel tfd]
    syscall
    jmp t10
t9_fail:
    lea rsi, [rel msg_t9_fail]
    mov rdx, msg_t9_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t10: ; unlink a freshly created file via creat + unlink (85, 87)
    mov rax, 85
    mov rdi, del_file
    syscall
    cmp rax, 0
    jl t10_fail
    mov [rel del_fd], rax
    mov rax, 3
    mov rdi, [rel del_fd]
    syscall
    mov rax, 87
    mov rdi, del_file
    syscall
    cmp rax, 0
    jne t10_fail
    lea rsi, [rel msg_t10]
    mov rdx, msg_t10_len
    mov rax, 1
    mov rdi, 1
    syscall
    jmp t11
t10_fail:
    lea rsi, [rel msg_t10_fail]
    mov rdx, msg_t10_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

t11: ; clone with CLONE_PARENT_SETTID|CLONE_CHILD_SETTID (56)
    mov rdi, 0x00008000 | 0x01000000
    lea rsi, [rel child_stack + 8192]
    lea rdx, [rel parent_tid]
    lea r10, [rel child_tid]
    lea r8, [rel tls_area]
    mov rax, 56
    syscall
    cmp rax, 0
    jl t11_fail
    mov [rel child_pid], rax
    lea rsi, [rel msg_t11]
    mov rdx, msg_t11_len
    mov rax, 1
    mov rdi, 1
    syscall
    mov rax, [rel child_pid]
    call print_hex
    jmp done
t11_fail:
    lea rsi, [rel msg_t11_fail]
    mov rdx, msg_t11_fail_len
    mov rax, 1
    mov rdi, 1
    syscall

done:
    mov rax, 231            ; exit_group(0)
    mov rdi, 0
    syscall

; ------------------------------------------------------------------
; print_hex: prints rax as 16 hex digits + newline to stdout
; clobbers: rax, rbx, rdx, rdi, rsi, r8, r11
; NOTE: rcx is clobbered by syscall, so the loop counter must be r8
; ------------------------------------------------------------------
print_hex:
    mov rbx, rax
    mov r8, 16
.hex_loop:
    mov rax, rbx
    shr rax, 60
    and rax, 0xF
    lea rsi, [rel hex_digits]
    add rsi, rax
    lea rdi, [rel hex_char]
    mov al, [rsi]
    mov [rdi], al
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel hex_char]
    mov rdx, 1
    syscall
    shl rbx, 4
    dec r8
    jnz .hex_loop
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel hex_nl]
    mov rdx, 1
    syscall
    ret

section .data
msg_t0:    db "T0 write ok", 0x0A
msg_t0_len equ $ - msg_t0
msg_t1:    db "T1 brk new="
msg_t1_len equ $ - msg_t1
msg_t2:    db "T2 cwd="
msg_t2_len equ $ - msg_t2
msg_t2_fail:    db "T2 getcwd FAIL", 0x0A
msg_t2_fail_len equ $ - msg_t2_fail
msg_t3:    db "T3 cwd after chdir="
msg_t3_len equ $ - msg_t3
msg_t3_fail:    db "T3 chdir FAIL", 0x0A
msg_t3_fail_len equ $ - msg_t3_fail
msg_t4:    db "T4 stat / mode="
msg_t4_len equ $ - msg_t4
msg_t4_fail:    db "T4 stat FAIL", 0x0A
msg_t4_fail_len equ $ - msg_t4_fail
msg_t5:    db "T5 getdents64 len="
msg_t5_len equ $ - msg_t5
msg_t5_fail:    db "T5 getdents64 FAIL", 0x0A
msg_t5_fail_len equ $ - msg_t5_fail
msg_t6:    db "T6 prctl name="
msg_t6_len equ $ - msg_t6
msg_t6_fail:    db "T6 prctl FAIL", 0x0A
msg_t6_fail_len equ $ - msg_t6_fail
msg_t7:    db "T7 mmap fixed value="
msg_t7_len equ $ - msg_t7
msg_t7_fail:    db "T7 mmap FAIL", 0x0A
msg_t7_fail_len equ $ - msg_t7_fail
msg_t8:    db "T8 memfd fstat size="
msg_t8_len equ $ - msg_t8
msg_t8_fail:    db "T8 memfd FAIL", 0x0A
msg_t8_fail_len equ $ - msg_t8_fail
msg_t9:    db "T9 timerfd expirations="
msg_t9_len equ $ - msg_t9
msg_t9_fail:    db "T9 timerfd FAIL", 0x0A
msg_t9_fail_len equ $ - msg_t9_fail
msg_t10:    db "T10 creat+unlink ok", 0x0A
msg_t10_len equ $ - msg_t10
msg_t10_fail:    db "T10 unlink FAIL", 0x0A
msg_t10_fail_len equ $ - msg_t10_fail
msg_t11:    db "T11 clone tid="
msg_t11_len equ $ - msg_t11
msg_t11_fail:    db "T11 clone FAIL", 0x0A
msg_t11_fail_len equ $ - msg_t11_fail
msg_ok:    db " OK", 0x0A
msg_ok_len equ $ - msg_ok
msg_fail:    db " FAIL", 0x0A
msg_fail_len equ $ - msg_fail

dir_root:       db "/", 0
dir_userland:   db "Userland", 0
memfd_name:     db "testmem", 0
name_set:       db "linuxhello", 0
del_file:       db "del_me_linux.txt", 0
hex_digits:     db "0123456789abcdef"
hex_char:       db 0
hex_nl:         db 0x0A
memfd_data:     db "0123456789abcdef"
tls_area:       times 64 db 0

section .bss
brk_old:        resq 1
brk_new:        resq 1
cwd_buf:        resb 256
stat_buf:       resb 144
stat_mode:      resq 1
dir_fd:         resq 1
dents_buf:      resb 4096
dents_len:      resq 1
name_get:       resb 16
memfd:          resq 1
memfd_readback: resb 16
tfd:            resq 1
it_value:       resq 2
it_interval:    resq 2
req_ts:         resq 2
expirations:    resq 1
del_fd:         resq 1
parent_tid:     resq 1
child_tid:      resq 1
child_pid:      resq 1
child_stack:    resb 8192
