/* timer.c - Programmable Interval Timer on IRQ0 */
#include "timer.h"
#include "idt.h"
#include "io.h"

#define PIT_CMD      0x43
#define PIT_CH0      0x40
#define PIT_BASE_HZ  1193182

static volatile uint32_t ticks;

static void timer_irq_handler(void)
{
    ticks++;
}

void timer_init(void)
{
    uint32_t divisor = PIT_BASE_HZ / TIMER_HZ;
    outb(PIT_CMD, 0x36);                          /* channel 0, lobyte/hibyte, rate generator */
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
    irq_register_handler(0, timer_irq_handler);
}

uint32_t timer_ticks(void)
{
    return ticks;
}

void timer_sleep_ms(uint32_t ms)
{
    uint32_t target = ticks + (ms * TIMER_HZ + 999) / 1000;
    while ((int32_t)(ticks - target) < 0) {
        __asm__ volatile ("hlt");
    }
}