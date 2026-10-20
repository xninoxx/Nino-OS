/* rtl8139.h - Realtek RTL8139 network card driver (QEMU: -device rtl8139), polling mode */
#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include <stdbool.h>

bool rtl8139_init(uint8_t mac[6]);                       /* false if no card is present */
void rtl8139_poll(void (*handler)(const uint8_t *frame, uint32_t len));   /* deliver all received frames */
uint32_t rtl8139_resets(void);                           /* how often a bad frame forced a receiver reset */
bool rtl8139_send(const uint8_t *frame, uint32_t len);   /* pads to 60 bytes; len <= 1514 */

#endif /* RTL8139_H */
