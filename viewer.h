/* viewer.h - read-only text viewer app (scrolls with the mouse wheel) */
#ifndef VIEWER_H
#define VIEWER_H

#include "fs.h"

void viewer_open(fs_node_t *file);     /* shows the file in the Viewer window */
void viewer_draw(int cx, int cy, int cw, int ch);
void viewer_scroll(int dir);           /* +1 = down; one notch of the mouse wheel */

#endif /* VIEWER_H */