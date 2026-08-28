BITS 32
section .text
global _start
extern bootmanager_bios_main

_start:
    jmp bootmanager_bios_main
