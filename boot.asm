; boot.asm - NinoOS entry point (Multiboot 1, requests a 1024x768x32 framebuffer)
bits 32

MBALIGN  equ 1 << 0                      ; align modules on page boundaries
MEMINFO  equ 1 << 1                      ; request memory map
VIDEO    equ 1 << 2                      ; request a graphics mode
FLAGS    equ MBALIGN | MEMINFO | VIDEO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0, 0, 0, 0, 0     ; header_addr, load_addr, load_end_addr, bss_end_addr, entry_addr (unused for ELF)
    dd 0                 ; mode_type: 0 = linear graphics
    dd 1024              ; width
    dd 768               ; height
    dd 32                ; depth (bits per pixel)

section .bss nobits
align 16
stack_bottom:
    resb 131072
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov  esp, stack_top
    cld
    push ebx                 ; arg 2: pointer to Multiboot info structure
    push eax                 ; arg 1: Multiboot magic (0x2BADB002)
    call kernel_main

    cli
.hang:
    hlt
    jmp  .hang

section .note.GNU-stack noalloc noexec nowrite progbits