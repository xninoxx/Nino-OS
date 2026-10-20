/* idt.h - IDT, PIC and interrupt dispatch */
#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Registers as pushed by isr_common in isr.asm (lowest address first) */
struct registers {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;  /* pusha */
    uint32_t int_no, err_code;                              /* pushed by stub */
    uint32_t eip, cs, eflags;                               /* pushed by CPU */
};

typedef void (*irq_handler_t)(void);

void pic_remap(void);                                       /* IRQ 0-15 -> vectors 0x20-0x2F, all masked */
void idt_init(void);                                        /* build and load the IDT */
void irq_register_handler(uint8_t irq, irq_handler_t fn);   /* register + unmask IRQ 0-15 */

#endif /* IDT_H */