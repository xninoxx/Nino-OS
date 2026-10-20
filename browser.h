/* browser.h - Nino Browser: a text-mode web browser for http:// pages */
#ifndef BROWSER_H
#define BROWSER_H

#include <stdbool.h>

#define BROWSER_CLIENT_W  920
#define BROWSER_CLIENT_H  (32 + 20 * 16 + 18)    /* toolbar + 20 text rows + status line */

void browser_init(void);
void browser_open(const char *url);              /* opens the window; url may be 0 for the home page */
void browser_draw(int cx, int cy, int cw, int ch);
void browser_click(int rx, int ry, bool dbl);
void browser_key(char c);
void browser_scroll(int dir);

#endif /* BROWSER_H */
