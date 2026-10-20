/* linker.ld - NinoKernel linker script */
ENTRY(_start)

SECTIONS
{
    /* The kernel is loaded at 1 MB */
    . = 1M;

    /* Multiboot header first, so it sits within the first 8 KB */
    .multiboot : ALIGN(4)
    {
        KEEP(*(.multiboot))
    }

    .text : ALIGN(4K)
    {
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note*)
    }
}