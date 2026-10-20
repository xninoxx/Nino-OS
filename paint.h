/* paint.h - Draw, the NinoOS paint program */
#ifndef PAINT_H
#define PAINT_H

#include <stdbool.h>

#define PAINT_CANVAS_W  512
#define PAINT_CANVAS_H  288
#define PAINT_TOOLBAR_H 32

void paint_init(void);
void paint_draw(int cx, int cy, int cw, int ch);
void paint_click(int rx, int ry, bool dbl);   /* mouse down, relative to the client area */
void paint_drag(int rx, int ry);              /* mouse moved with the button held */

#endif /* PAINT_H */
