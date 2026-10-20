/* widgets.h - small reusable UI pieces: buttons and scrollbars */
#ifndef WIDGETS_H
#define WIDGETS_H

#include <stdbool.h>

#define SCROLLBAR_W 12

bool ui_inside(int px, int py, int x, int y, int w, int h);

/* 3D push button; the label shrinks to a smaller font if it does not fit */
void ui_button(int x, int y, int w, int h, const char *label, bool pressed);

/* Vertical scrollbar (only drawn when total > visible) */
void ui_scrollbar(int x, int y, int h, int total, int visible, int offset);

/* Clamp a scroll offset to 0..total-visible */
int  ui_clamp_scroll(int offset, int total, int visible);

#endif /* WIDGETS_H */
