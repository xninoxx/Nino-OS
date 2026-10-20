/* panic.c - blue crash screen. Draws straight through the graphics driver (no allocations, no
 * interrupts) so it still works when the rest of the kernel is in a bad state. */
#include "panic.h"
#include "gfx.h"
#include "vga.h"
#include "io.h"
#include "power.h"
#include "sysinfo.h"
#include "string.h"

#define NINO_VERSION_TEXT "NinoOS 0.7"

static void hex32(char *out, uint32_t v)
{
    static const char d[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        out[i] = d[(v >> (28 - 4 * i)) & 0xF];
    }
    out[8] = '\0';
}

static void line_reg(char *buf, const char *label, uint32_t v)
{
    int n = 0;
    while (label[n]) { buf[n] = label[n]; n++; }
    buf[n++] = '=';
    hex32(buf + n, v);
}

static void wait_for_reboot_key(void)
{
    __asm__ volatile ("cli");
    for (;;) {
        if (inb(0x64) & 0x01) {
            uint8_t sc = inb(0x60);
            if (sc == 0x13) {                            /* the R key */
                outb(0x64, 0xFE);                        /* reset */
            }
        }
        __asm__ volatile ("pause");
    }
}

void panic_exception(const struct registers *regs, const char *name)
{
    __asm__ volatile ("cli");
    char b[40];

    if (gfx_width() > 0) {
        int w = gfx_width();
        gfx_fill_rect(0, 0, w, gfx_height(), 0x1030A0);
        gfx_draw_text(32, 24, "NinoOS has crashed :(", 0xFFFFFF, 3);
        gfx_draw_text(32, 72, name, 0xFFE070, 2);
        gfx_draw_text(32, 104, NINO_VERSION_TEXT, 0xC0D0FF, 2);

        int y = 160;
        line_reg(b, "EIP", regs->eip);        gfx_draw_text(32, y, b, 0xFFFFFF, 2);
        line_reg(b, "ERR", regs->err_code);   gfx_draw_text(300, y, b, 0xFFFFFF, 2);
        line_reg(b, "INT", regs->int_no);     gfx_draw_text(570, y, b, 0xFFFFFF, 2); y += 32;
        line_reg(b, "EAX", regs->eax);        gfx_draw_text(32, y, b, 0xC0D0FF, 2);
        line_reg(b, "EBX", regs->ebx);        gfx_draw_text(300, y, b, 0xC0D0FF, 2);
        line_reg(b, "ECX", regs->ecx);        gfx_draw_text(570, y, b, 0xC0D0FF, 2); y += 32;
        line_reg(b, "EDX", regs->edx);        gfx_draw_text(32, y, b, 0xC0D0FF, 2);
        line_reg(b, "ESI", regs->esi);        gfx_draw_text(300, y, b, 0xC0D0FF, 2);
        line_reg(b, "EDI", regs->edi);        gfx_draw_text(570, y, b, 0xC0D0FF, 2); y += 32;
        line_reg(b, "EBP", regs->ebp);        gfx_draw_text(32, y, b, 0xC0D0FF, 2);
        line_reg(b, "CS", regs->cs);          gfx_draw_text(300, y, b, 0xC0D0FF, 2);
        line_reg(b, "EFLAGS", regs->eflags);  gfx_draw_text(570, y, b, 0xC0D0FF, 2); y += 64;

        gfx_draw_text(32, y, "Please send a photo or screenshot of this screen", 0xFFFFFF, 2); y += 24;
        gfx_draw_text(32, y, "to the person who made NinoOS, and tell them what", 0xFFFFFF, 2); y += 24;
        gfx_draw_text(32, y, "you were doing just before it happened.", 0xFFFFFF, 2); y += 48;
        gfx_draw_text(32, y, "Your saved files are safe. Press R to restart.", 0xFFE070, 2);
        gfx_flip();
    } else {
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
        vga_puts("\n*** NinoOS has crashed: ");
        vga_puts(name);
        vga_puts(" ***\n");
        line_reg(b, "EIP", regs->eip);      vga_puts(b);
        line_reg(b, "  ERR", regs->err_code); vga_puts(b);
        vga_puts("\nPress R to restart.\n");
    }
    wait_for_reboot_key();
    for (;;) { __asm__ volatile ("hlt"); }
}

void fatal_error(const char *title, const char *l1, const char *l2, const char *l3)
{
    __asm__ volatile ("cli");
    if (gfx_width() > 0) {
        gfx_fill_rect(0, 0, gfx_width(), gfx_height(), 0x601010);
        gfx_draw_text(32, 32, title, 0xFFFFFF, 3);
        gfx_draw_text(32, 100, l1 ? l1 : "", 0xFFE0E0, 2);
        gfx_draw_text(32, 132, l2 ? l2 : "", 0xFFE0E0, 2);
        gfx_draw_text(32, 164, l3 ? l3 : "", 0xFFE0E0, 2);
        gfx_draw_text(32, 232, NINO_VERSION_TEXT, 0xC09090, 2);
        gfx_flip();
    } else {
        vga_init();
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
        vga_puts(title);
        vga_puts("\n\n");
        if (l1) { vga_puts(l1); vga_puts("\n"); }
        if (l2) { vga_puts(l2); vga_puts("\n"); }
        if (l3) { vga_puts(l3); vga_puts("\n"); }
    }
    for (;;) { __asm__ volatile ("hlt"); }
}
