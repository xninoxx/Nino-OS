/* pci.h - PCI configuration space access (legacy port 0xCF8/0xCFC mechanism) */
#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint8_t  class_code, subclass;
} pci_dev_t;

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v);
bool     pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out);
int      pci_scan(pci_dev_t *list, int max);     /* all devices on buses 0-7 */

#endif /* PCI_H */
