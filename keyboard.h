/* keyboard.h - PS/2 keyboard driver (IRQ1) */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>

/* Special keys are delivered as values >= 0x80 (negative as char). Code that only handles
 * printable characters (32..126) ignores them automatically. */
#define KEY_UP      ((char)0x80)
#define KEY_DOWN    ((char)0x81)
#define KEY_LEFT    ((char)0x82)
#define KEY_RIGHT   ((char)0x83)
#define KEY_HOME    ((char)0x84)
#define KEY_END     ((char)0x85)
#define KEY_DELETE  ((char)0x86)
#define KEY_PGUP    ((char)0x87)
#define KEY_PGDN    ((char)0x88)
#define KEY_CTRL_S  ((char)0x13)    /* Ctrl+S */

void keyboard_init(void);                          /* register IRQ1 handler and unmask the line */
void keyboard_set_callback(void (*cb)(char));      /* receive typed characters; default: print to VGA */

/* Layouts: "us" (also fine for US-International), "de" (QWERTZ, AltGr works), "dvorak" */
bool        keyboard_set_layout(const char *name);
const char *keyboard_layout(void);

#endif /* KEYBOARD_H */
