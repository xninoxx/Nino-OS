/* gui.h - NinoOS window manager and shared UI constants */
#ifndef GUI_H
#define GUI_H

/* Text size shared by all apps: 8x8 font at scale 2 = 16x16 pixels per character */
#define FONT_SCALE  2
#define CHAR_W      (8 * FONT_SCALE)
#define CHAR_H      (8 * FONT_SCALE)

/* Dark "terminal" colors, also used by Fastfetch (0x00RRGGBB) */
#define COL_TERM_BG 0x101018
#define COL_TERM_FG 0xA0FFA0

/* Window ids */
enum {
    WIN_TERMINAL,
    WIN_CAPYBARA,
    WIN_VIEWER,
    WIN_FASTFETCH,
    WIN_DRAW,
    WIN_BROWSER,
    WIN_EDITOR,
    WIN_ABOUT,
    WIN_COUNT
};

void gui_init(void);
void gui_key_input(char c);                    /* keyboard callback (called from IRQ1) */
void gui_run(void) __attribute__((noreturn));
void gui_open_window(int id);                  /* show + bring to front */
void gui_redraw(void);                          /* draw + show now (use before a long blocking operation) */
void gui_set_title(int id, const char *title); /* title must stay valid (static buffer) */

#endif /* GUI_H */