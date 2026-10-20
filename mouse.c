/* mouse.c - PS/2 mouse driver */
#include "mouse.h"
#include "idt.h"
#include "io.h"

#define KBC_DATA     0x60
#define KBC_STATUS   0x64     /* read: status, write: command */

static volatile int     pos_x;
static volatile int     pos_y;
static volatile uint8_t btn_state;
static volatile bool    changed;
static volatile int     wheel_acc;
static bool             has_wheel;     /* IntelliMouse: 4-byte packets */

static int     max_x;
static int     max_y;
static uint8_t packet[4];
static int     cycle;

static void kbc_wait_write(void)   /* wait until the controller accepts input */
{
    for (uint32_t t = 100000; t && (inb(KBC_STATUS) & 0x02); t--) { }
}

static void kbc_wait_read(void)    /* wait until output is available */
{
    for (uint32_t t = 100000; t && !(inb(KBC_STATUS) & 0x01); t--) { }
}

static void mouse_write(uint8_t value)
{
    kbc_wait_write();
    outb(KBC_STATUS, 0xD4);        /* next byte goes to the mouse */
    kbc_wait_write();
    outb(KBC_DATA, value);
}

static uint8_t mouse_read(void)
{
    kbc_wait_read();
    return inb(KBC_DATA);
}

static void mouse_irq_handler(void)
{
    uint8_t b = inb(KBC_DATA);
    int pkt_len = has_wheel ? 4 : 3;

    if (cycle == 0 && !(b & 0x08)) {   /* bit 3 is always set in byte 0: resync if not */
        return;
    }
    packet[cycle++] = b;
    if (cycle < pkt_len) {
        return;
    }
    cycle = 0;

    if (packet[0] & 0xC0) {            /* x/y overflow: drop this packet */
        return;
    }

    int dx = packet[1];
    int dy = packet[2];
    if (packet[0] & 0x10) dx -= 256;   /* sign bits */
    if (packet[0] & 0x20) dy -= 256;

    int z = 0;
    if (has_wheel) {
        z = packet[3] & 0x0F;          /* 4-bit signed: +1 = towards you */
        if (z & 0x08) z -= 16;
    }

    int nx = pos_x + dx;
    int ny = pos_y - dy;               /* PS/2 y axis points up */
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > max_x) nx = max_x;
    if (ny > max_y) ny = max_y;

    pos_x      = nx;
    pos_y      = ny;
    btn_state  = packet[0] & 0x07;
    wheel_acc += z;
    changed    = true;
}

void mouse_init(int screen_w, int screen_h)
{
    max_x = screen_w - 1;
    max_y = screen_h - 1;
    pos_x = screen_w / 2;
    pos_y = screen_h / 2;

    /* Drain stale bytes */
    while (inb(KBC_STATUS) & 0x01) {
        (void)inb(KBC_DATA);
    }

    kbc_wait_write();
    outb(KBC_STATUS, 0xA8);                 /* enable the auxiliary (mouse) port */

    kbc_wait_write();
    outb(KBC_STATUS, 0x20);                 /* read controller configuration byte */
    kbc_wait_read();
    uint8_t cfg = inb(KBC_DATA);
    cfg |= 0x02;                            /* enable IRQ12 */
    cfg &= (uint8_t)~0x20;                  /* enable mouse clock */
    kbc_wait_write();
    outb(KBC_STATUS, 0x60);                 /* write configuration byte */
    kbc_wait_write();
    outb(KBC_DATA, cfg);

    mouse_write(0xF6);                      /* defaults */
    (void)mouse_read();                     /* ACK */

    /* Magic sample-rate sequence 200, 100, 80 switches an IntelliMouse into wheel mode */
    static const uint8_t magic[3] = { 200, 100, 80 };
    for (int i = 0; i < 3; i++) {
        mouse_write(0xF3);                  /* set sample rate */
        (void)mouse_read();                 /* ACK */
        mouse_write(magic[i]);
        (void)mouse_read();                 /* ACK */
    }
    mouse_write(0xF2);                      /* get device id */
    (void)mouse_read();                     /* ACK */
    has_wheel = (mouse_read() == 3);

    mouse_write(0xF4);                      /* enable data reporting */
    (void)mouse_read();                     /* ACK */

    irq_register_handler(12, mouse_irq_handler);
}

bool mouse_poll(int *x, int *y, uint8_t *buttons, int *wheel)
{
    if (!changed) {
        return false;
    }
    *x = pos_x;
    *y = pos_y;
    *buttons = btn_state;
    *wheel = wheel_acc;
    wheel_acc = 0;
    changed = false;
    return true;
}