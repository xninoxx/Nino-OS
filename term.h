/* term.h - built-in terminal app */
#ifndef TERM_H
#define TERM_H

#define TERM_COLS  44
#define TERM_ROWS  17
#define TERM_PAD   4

void term_init(void);
void term_draw(int cx, int cy, int cw, int ch);
void term_key(char c);

#endif /* TERM_H */