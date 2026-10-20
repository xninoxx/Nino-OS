/* editor.h - Notepad: a simple text editor */
#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include "fs.h"

#define EDITOR_CLIENT_W  800
#define EDITOR_CLIENT_H  (32 + 20 * 16 + 18)

void editor_init(void);
void editor_open(fs_node_t *file);               /* opens the window with this file (0 = empty new file) */
void editor_draw(int cx, int cy, int cw, int ch);
void editor_click(int rx, int ry, bool dbl);
void editor_key(char c);
void editor_scroll(int dir);

#endif /* EDITOR_H */
