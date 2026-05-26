OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
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
