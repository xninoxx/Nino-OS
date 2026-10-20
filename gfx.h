/* gfx.h - 32-bit linear framebuffer driver with a back buffer */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include <stdbool.h>
#include "multiboot.h"

#define GFX_MAX_WIDTH   1024
#define GFX_MAX_HEIGHT  768

/* Returns false if the bootloader gave us no usable 32-bpp framebuffer */
bool gfx_init(const multiboot_info_t *mbi);

int  gfx_width(void);
int  gfx_height(void);

/* All drawing goes to the back buffer; call gfx_flip() to show it. Colors are 0x00RRGGBB. */
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);   /* 1px outline */
void gfx_draw_char(int x, int y, char c, uint32_t color, int scale);
void gfx_draw_text(int x, int y, const char *s, uint32_t color, int scale);
void gfx_flip(void);

#endif /* GFX_H */