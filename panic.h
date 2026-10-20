/* panic.h - crash screen (works in graphics mode and in text mode) */
#ifndef PANIC_H
#define PANIC_H

#include "idt.h"

/* A CPU exception happened: show a bug-report screen and wait for 'R' (reboot) */
void panic_exception(const struct registers *regs, const char *name) __attribute__((noreturn));

/* A fatal start-up problem: show up to three lines and stop */
void fatal_error(const char *title, const char *l1, const char *l2, const char *l3) __attribute__((noreturn));

#endif /* PANIC_H */
