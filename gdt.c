/* gdt.c - own GDT so we no longer depend on the bootloader's one */
#include "gdt.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void gdt_flush(uint32_t gdt_ptr_addr);   /* gdt_flush.asm */

static struct gdt_entry gdt[3];
static struct gdt_ptr   gp;

static void gdt_set_gate(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].base_low   = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid   = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_high  = (uint8_t)((base >> 24) & 0xFF);
    gdt[i].limit_low  = (uint16_t)(limit & 0xFFFF);
    gdt[i].gran       = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access     = access;
}

void gdt_init(void)
{
    gdt_set_gate(0, 0, 0,       0x00, 0x00);   /* null descriptor */
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);   /* 0x08: kernel code, ring 0, 4 GB */
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);   /* 0x10: kernel data, ring 0, 4 GB */

    gp.limit = (uint16_t)(sizeof(gdt) - 1);
    gp.base  = (uint32_t)&gdt;
    gdt_flush((uint32_t)&gp);
}