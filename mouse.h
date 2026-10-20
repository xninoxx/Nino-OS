/* mouse.h - PS/2 mouse driver (IRQ12), with scroll wheel support (IntelliMouse) */
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

void mouse_init(int screen_w, int screen_h);

/* Returns true if the mouse moved, a button changed or the wheel turned since the last call.
 * Buttons: bit 0 = left, bit 1 = right, bit 2 = middle.
 * wheel: notches since the last call, positive = scroll down (wheel towards you).
 * Call with interrupts disabled (the IRQ handler updates the same state). */
bool mouse_poll(int *x, int *y, uint8_t *buttons, int *wheel);

#endif /* MOUSE_H */
