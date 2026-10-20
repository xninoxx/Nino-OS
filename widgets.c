/* widgets.c - small reusable UI pieces */
#include "widgets.h"
#include "gfx.h"
#include "gui.h"
#include "string.h"

bool ui_inside(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void ui_button(int x, int y, int w, int h, const char *label, bool pressed)
{
    gfx_fill_rect(x, y, w, h, pressed ? 0xA8A8A8 : 0xD8D8D8);
    gfx_fill_rect(x, y, w, 1, pressed ? 0x606060 : 0xFFFFFF);          /* top highlight */
    gfx_fill_rect(x, y, 1, h, pressed ? 0x606060 : 0xFFFFFF);          /* left highlight */
    gfx_fill_rect(x, y + h - 1, w, 1, pressed ? 0xFFFFFF : 0x606060);  /* bottom shadow */
    gfx_fill_rect(x + w - 1, y, 1, h, pressed ? 0xFFFFFF : 0x606060);  /* right shadow */

    int len = (int)strlen(label);
    int scale = FONT_SCALE;
    if (len * 8 * scale + 8 > w) {
        scale = 1;
    }
    int tw = len * 8 * scale;
    int th = 8 * scale;
    gfx_draw_text(x + (w - tw) / 2 + (pressed ? 1 : 0), y + (h - th) / 2 + (pressed ? 1 : 0),
                  label, 0x000000, scale);
}

int ui_clamp_scroll(int offset, int total, int visible)
{
    int max = total - visible;
    if (max < 0) {
        max = 0;
    }
    if (offset > max) {
        offset = max;
    }
    if (offset < 0) {
        offset = 0;
    }
    return offset;
}

void ui_scrollbar(int x, int y, int h, int total, int visible, int offset)
{
    if (total <= visible || visible <= 0 || h < 24) {
        return;
    }
    gfx_fill_rect(x, y, SCROLLBAR_W, h, 0xDCDCDC);
    gfx_draw_rect(x, y, SCROLLBAR_W, h, 0x909090);

    int thumb_h = h * visible / total;
    if (thumb_h < 16) {
        thumb_h = 16;
    }
    int range = h - thumb_h;
    int max   = total - visible;
    int ty    = y + (max > 0 ? range * offset / max : 0);
    gfx_fill_rect(x + 1, ty, SCROLLBAR_W - 2, thumb_h, 0x7A8FB5);
    gfx_draw_rect(x + 1, ty, SCROLLBAR_W - 2, thumb_h, 0x3A4A6A);
}
