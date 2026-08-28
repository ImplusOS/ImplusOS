OUTPUT_FORMAT(elf32-i386)
ENTRY(bios_stage2_start)

SECTIONS
{
    . = 0x8000;

    .text : {
        *(.text.stage2)
        *(.data.stage2)
        *(.bss.stage2)
        *(.text .text.*)
    }

    .rodata ALIGN(16) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(16) : {
        *(.data .data.*)
    }

    .bss ALIGN(16) : {
        *(COMMON)
        *(.bss .bss.*)
    }
}
