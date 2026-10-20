/* power.c - shutdown (ACPI power-off ports of the common virtual machines) and reboot */
#include "power.h"
#include "io.h"
#include "persist.h"
#include "gfx.h"
#include "gui.h"

void power_reboot(void)
{
    if (persist_present()) {
        persist_save();                                  /* make sure the latest changes are on disk */
    }
    __asm__ volatile ("cli");
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) break;                  /* wait until the keyboard controller is ready */
    }
    outb(0x64, 0xFE);                                    /* pulse the CPU reset line */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void power_off(void)
{
    if (persist_present()) {
        persist_save();
    }
    __asm__ volatile ("cli");

    outw(0x604, 0x2000);                                 /* QEMU (q35 / newer i440fx) */
    outw(0x4004, 0x3400);                                /* VirtualBox */
    outw(0xB004, 0x2000);                                /* Bochs and older QEMU */
    outw(0x600, 0x34);                                   /* some Cloud Hypervisor / other ACPI setups */

    /* Still running: this machine has no software power-off we know. Say so and stop. */
    if (gfx_width() > 0) {
        gfx_fill_rect(0, 0, gfx_width(), gfx_height(), 0x000000);
        gfx_draw_text(32, 32, "It is now safe to turn off the computer.", 0xFFB000, 2);
        gfx_draw_text(32, 64, "(Close the window of the virtual machine.)", 0xA0A0A0, 2);
        gfx_flip();
    }
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
