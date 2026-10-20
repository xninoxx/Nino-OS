/* timer.h - PIT timer (IRQ0) */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define TIMER_HZ 1000                /* 1000 ticks per second = 1 ms per tick (fast network polling) */

void     timer_init(void);
uint32_t timer_ticks(void);          /* ticks since boot */
void     timer_sleep_ms(uint32_t ms);/* needs interrupts enabled (sti) */

#endif /* TIMER_H */