/* gfx.c - 32-bit linear framebuffer driver with a back buffer */
#include "gfx.h"
#include "font8x8.h"

/* Back buffer in RAM (3 MB): we draw here and copy to video memory in one go,
 * which avoids flicker. */
static uint32_t backbuffer[GFX_MAX_WIDTH * GFX_MAX_HEIGHT];

static uint8_t *fb_base;
static uint32_t fb_pitch;
static int      fb_w;
static int      fb_h;

static inline void fill32(uint32_t *dst, uint32_t value, uint32_t count)
{
    __asm__ volatile ("rep stosl" : "+D"(dst), "+c"(count) : "a"(value) : "memory");
}

static inline void copy32(void *dst, const void *src, uint32_t count)
{
    __asm__ volatile ("rep movsl" : "+D"(dst), "+S"(src), "+c"(count) : : "memory");
}

bool gfx_init(const multiboot_info_t *mbi)
{
    if (mbi == 0 || !(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        return false;
    }
    if (mbi->framebuffer_type != 1 || mbi->framebuffer_bpp != 32) {
        return false;
    }
    if (mbi->framebuffer_addr_high != 0) {
        return false;                      /* above 4 GB: unreachable without paging */
    }
    if (mbi->framebuffer_width > GFX_MAX_WIDTH || mbi->framebuffer_height > GFX_MAX_HEIGHT) {
        return false;
    }

    fb_base  = (uint8_t *)mbi->framebuffer_addr_low;
    fb_pitch = mbi->framebuffer_pitch;
    fb_w     = (int)mbi->framebuffer_width;
    fb_h     = (int)mbi->framebuffer_height;
    return true;
}

int gfx_width(void)  { return fb_w; }
int gfx_height(void) { return fb_h; }

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    /* Clip to the screen */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) { w = fb_w - x; }
    if (y + h > fb_h) { h = fb_h - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int row = 0; row < h; row++) {
        fill32(&backbuffer[(y + row) * fb_w + x], color, (uint32_t)w);
    }
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
}

void gfx_draw_char(int x, int y, char c, uint32_t color, int scale)
{
    if (c < 32 || c > 126) {
        return;
    }
    const uint8_t *glyph = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void gfx_draw_text(int x, int y, const char *s, uint32_t color, int scale)
{
    while (*s) {
        gfx_draw_char(x, y, *s++, color, scale);
        x += 8 * scale;
    }
}

void gfx_flip(void)
{
    for (int y = 0; y < fb_h; y++) {
        copy32(fb_base + (uint32_t)y * fb_pitch, &backbuffer[y * fb_w], (uint32_t)fb_w);
    }
}