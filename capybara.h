/* capybara.h - Capybara, the NinoOS file explorer */
#ifndef CAPYBARA_H
#define CAPYBARA_H

#include <stdbool.h>

void capybara_init(void);
void capybara_draw(int cx, int cy, int cw, int ch);
void capybara_click(int rx, int ry, bool dbl);   /* rx, ry relative to the client area */
void capybara_scroll(int dir);                   /* +1 = down */
bool capybara_wants_keys(void);                  /* true while renaming */
void capybara_key(char c);

#endif /* CAPYBARA_H */