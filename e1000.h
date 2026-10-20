/* e1000.h - Intel PRO/1000 (82540EM and friends) network driver: VirtualBox's default card, also in QEMU */
#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stdbool.h>

bool e1000_init(uint8_t mac[6]);                         /* false if no such card is present */
void e1000_poll(void (*handler)(const uint8_t *frame, uint32_t len));
bool e1000_send(const uint8_t *frame, uint32_t len);

#endif /* E1000_H */
