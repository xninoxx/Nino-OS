/* kernel.c - NinoKernel main entry point */
#include <stdint.h>
#include <stdbool.h>
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "heap.h"
#include "timer.h"
#include "keyboard.h"
#include "multiboot.h"
#include "gfx.h"
#include "mouse.h"
#include "fs.h"
#include "gui.h"
#include "persist.h"
#include "sysinfo.h"
#include "panic.h"
#include "util.h"

struct sysinfo g_sysinfo;

void kernel_main(uint32_t magic, multiboot_info_t *mbi)
{
    bool mb_ok = (magic == MULTIBOOT_BOOTLOADER_MAGIC);
    bool gui_mode = mb_ok && gfx_init(mbi);

    if (mb_ok && (mbi->flags & 1)) {         /* bit 0: mem_lower/mem_upper are valid */
        g_sysinfo.ram_kb = mbi->mem_upper;
    }
    if (gui_mode) {
        g_sysinfo.screen_w = gfx_width();
        g_sysinfo.screen_h = gfx_height();
    }

    /* NinoOS keeps a 64 MB heap and a few big buffers in memory: it needs about 192 MB of RAM */
    if (mb_ok && (mbi->flags & 1) && mbi->mem_upper < 120u * 1024u) {
        char l1[80];
        char *p = put_str(l1, "NinoOS found only ");
        p = put_uint(p, mbi->mem_upper / 1024 + 1);
        put_str(p, " MB of RAM, and needs 256 MB.");
        fatal_error("Not enough memory", l1,
                    "Give the virtual machine more RAM:",
                    "VirtualBox: Settings > System > Base Memory = 256 MB");
    }

    gdt_init();          /* our own segments first: idt_init() uses the current CS */
    pic_remap();
    idt_init();
    heap_init();
    timer_init();
    keyboard_init();

    if (gui_mode) {
        keyboard_set_callback(gui_key_input);
        mouse_init(gfx_width(), gfx_height());
        fs_init();
        persist_init();                      /* loads files from disk.img if a disk is attached */
        gui_init();
        __asm__ volatile ("sti");
        gui_run();                           /* never returns */
    }

    /* No usable graphics mode: say why, in text mode */
    char l2[80] = "";
    if (mb_ok && (mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        char *p = put_str(l2, "Screen type ");
        p = put_uint(p, mbi->framebuffer_type);
        p = put_str(p, ", ");
        p = put_uint(p, mbi->framebuffer_bpp);
        put_str(p, " bits per pixel (needed: type 1, 32 bits)");
    } else {
        put_str(l2, "The boot loader did not set a graphics mode.");
    }
    fatal_error("NinoOS needs a graphics screen", l2,
                "VirtualBox: Settings > Display > Graphics Controller = VBoxVGA,",
                "Video Memory = 16 MB or more. Then start the VM again.");
}
