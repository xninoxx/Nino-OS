/* idt.c - Interrupt Descriptor Table, PIC remapping, interrupt dispatch */
#include "idt.h"
#include "io.h"
#include "vga.h"
#include "panic.h"

#define IDT_ENTRIES   256
#define PIC1_CMD      0x20
#define PIC1_DATA     0x21
#define PIC2_CMD      0xA0
#define PIC2_DATA     0xA1
#define PIC_EOI       0x20
#define PIC1_OFFSET   0x20      /* IRQ 0-7  -> vectors 0x20-0x27 */
#define PIC2_OFFSET   0x28      /* IRQ 8-15 -> vectors 0x28-0x2F */

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;
static irq_handler_t    irq_handlers[16];

extern uint32_t isr_stub_table[];   /* defined in isr.asm: 48 stub addresses */

static const char *exception_names[] = {
    "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Error", "Alignment Check", "Machine Check",
    "SIMD Floating-Point Exception", "Virtualization Exception",
    "Control Protection Exception"
};
#define EXCEPTION_NAME_COUNT (sizeof(exception_names) / sizeof(exception_names[0]))

static void idt_set_gate(uint8_t vector, uint32_t handler, uint16_t selector, uint8_t flags)
{
    idt[vector].base_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].base_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].selector  = selector;
    idt[vector].zero      = 0;
    idt[vector].flags     = flags;
}

void pic_remap(void)
{
    /* ICW1: start initialization, expect ICW4 */
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    /* ICW2: vector offsets */
    outb(PIC1_DATA, PIC1_OFFSET); io_wait();
    outb(PIC2_DATA, PIC2_OFFSET); io_wait();
    /* ICW3: slave PIC is attached to IRQ2 of the master */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    /* Mask every IRQ; drivers unmask their own line via irq_register_handler() */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void idt_init(void)
{
    /* Use whatever code segment the bootloader set up (flat 4 GB) */
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    /* 0x8E = present, ring 0, 32-bit interrupt gate */
    for (uint8_t i = 0; i < 48; i++) {
        idt_set_gate(i, isr_stub_table[i], cs, 0x8E);
    }

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}

void irq_register_handler(uint8_t irq, irq_handler_t fn)
{
    if (irq >= 16) {
        return;
    }
    irq_handlers[irq] = fn;

    /* Unmask the line; slave IRQs also need the cascade line (IRQ2) open */
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1 << irq));
    } else {
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1 << 2));
        outb(PIC2_DATA, inb(PIC2_DATA) & (uint8_t)~(1 << (irq - 8)));
    }
}

/* Called from isr_common (isr.asm) for every interrupt */
void isr_handler(struct registers *regs)
{
    if (regs->int_no < 32) {
        /* CPU exception: show the crash screen (never returns) */
        panic_exception(regs, regs->int_no < EXCEPTION_NAME_COUNT ? exception_names[regs->int_no]
                                                                   : "Reserved exception");
    }

    /* Hardware IRQ */
    uint8_t irq = (uint8_t)(regs->int_no - 32);
    if (irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }

    /* End of interrupt: slave first (if involved), then master */
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}