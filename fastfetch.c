/* fastfetch.c - fastfetch-style system info window */
#include "fastfetch.h"
#include "gui.h"
#include "gfx.h"
#include "logo.h"
#include "timer.h"
#include "heap.h"
#include "util.h"
#include "sysinfo.h"
#include "string.h"

#define KEY_COL  0x58C8F0
#define VAL_COL  0xE0E0E0
#define DIM_COL  0x707080
#define LINE_H   20

static void draw_row(int x, int y, const char *key, const char *value)
{
    gfx_draw_text(x, y, key, KEY_COL, FONT_SCALE);
    gfx_draw_text(x + (int)strlen(key) * CHAR_W, y, value, VAL_COL, FONT_SCALE);
}

void fastfetch_draw(int cx, int cy, int cw, int ch)
{
    (void)cw;
    gfx_fill_rect(cx, cy, cw, ch, COL_TERM_BG);

    /* Logo on the left: one 16x16 glyph per cell, colored per cell code */
    int lx = cx + 16;
    int ly = cy + 16;
    for (int i = 0; i < LOGO_LINES; i++) {
        for (int j = 0; nino_logo[i][j]; j++) {
            char g = logo_glyph(nino_logo[i][j]);
            if (g) {
                gfx_draw_char(lx + j * CHAR_W, ly + i * CHAR_H, g, logo_color(nino_logo[i][j]), FONT_SCALE);
            }
        }
    }

    /* Info block on the right */
    int ix = cx + 16 + LOGO_WIDTH * CHAR_W + 24;
    int y  = cy + 16;
    char buf[48];
    char *p;

    gfx_draw_text(ix, y, "nino@ninoos", KEY_COL, FONT_SCALE);
    y += LINE_H;
    gfx_draw_text(ix, y, "-----------", DIM_COL, FONT_SCALE);
    y += LINE_H;

    draw_row(ix, y, "OS: ", "NinoOS 0.7");
    y += LINE_H;

    draw_row(ix, y, "Kernel: ", "NinoKernel 0.7");
    y += LINE_H;

    put_uptime(buf, timer_ticks() / TIMER_HZ);
    draw_row(ix, y, "Uptime: ", buf);
    y += LINE_H;

    draw_row(ix, y, "Shell: ", "nino terminal");
    y += LINE_H;

    p = put_uint(buf, (uint32_t)g_sysinfo.screen_w);
    p = put_str(p, "x");
    put_uint(p, (uint32_t)g_sysinfo.screen_h);
    draw_row(ix, y, "Resolution: ", buf);
    y += LINE_H;

    p = put_uint(buf, (g_sysinfo.ram_kb + 1024) / 1024);
    put_str(p, " MiB");
    draw_row(ix, y, "RAM: ", buf);
    y += LINE_H;

    p = put_uint(buf, heap_used() / 1024);
    p = put_str(p, " / ");
    p = put_uint(p, heap_total() / 1024);
    put_str(p, " KiB");
    draw_row(ix, y, "Heap: ", buf);
    y += LINE_H;

    draw_row(ix, y, "Theme: ", "Nino Blue");
    y += LINE_H + 8;

    /* Color palette, like fastfetch */
    static const uint32_t palette[8] = {
        0x1E1E2E, 0xE05561, 0x8CC265, 0xE6B455, 0x4AA5F0, 0xC162DE, 0x42B3C2, 0xD7DAE0
    };
    for (int i = 0; i < 8; i++) {
        gfx_fill_rect(ix + i * 28, y, 28, 24, palette[i]);
    }
}