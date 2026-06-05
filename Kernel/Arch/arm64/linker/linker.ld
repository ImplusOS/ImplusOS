ENTRY(kernel_main)

SECTIONS
{
    . = 0x40080000;
    .text : ALIGN(4096) {
        *(.text.arm64_vectors)
        *(.text*)
    }
    .rodata : ALIGN(4096) { *(.rodata*) }
    .data : ALIGN(4096) { *(.data*) }
    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }
}
