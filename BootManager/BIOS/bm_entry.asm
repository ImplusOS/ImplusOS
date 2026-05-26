BITS 32
section .text
global _start
extern bootmanager_bios_main

_start:
    ; BiosLoader called us with entry(params), so params is at [esp + 4]
    ; and return address is at [esp].
    ; Jumping to bootmanager_bios_main will make it see the same stack.
    jmp bootmanager_bios_main
