/* nic.h - picks the network card driver: Intel e1000 (VirtualBox default) or Realtek RTL8139 (QEMU) */
#ifndef NIC_H
#define NIC_H

#include <stdint.h>
#include <stdbool.h>

bool        nic_init(uint8_t mac[6]);                    /* false if no supported card is found */
void        nic_poll(void (*handler)(const uint8_t *frame, uint32_t len));
bool        nic_send(const uint8_t *frame, uint32_t len);
const char *nic_name(void);                              /* "Intel e1000", "Realtek RTL8139" or "none" */
uint32_t    nic_resets(void);

#endif /* NIC_H */
