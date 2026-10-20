/* pci.c - PCI bus scan */
#include "pci.h"
#include "io.h"

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                ((uint32_t)func << 8) | (off & 0xFC));
    return inl(0xCFC);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v)
{
    outl(0xCF8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                ((uint32_t)func << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

int pci_scan(pci_dev_t *list, int max)
{
    int n = 0;
    for (int bus = 0; bus < 8; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id0 = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            if ((id0 & 0xFFFF) == 0xFFFF) {
                continue;                              /* no device in this slot */
            }
            uint32_t hdr = pci_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
            int funcs = (hdr & 0x00800000) ? 8 : 1;    /* multi-function device? */
            for (int f = 0; f < funcs; f++) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)f, 0);
                if ((id & 0xFFFF) == 0xFFFF) {
                    continue;
                }
                if (n < max) {
                    uint32_t cls = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)f, 0x08);
                    list[n].bus = (uint8_t)bus;
                    list[n].slot = (uint8_t)slot;
                    list[n].func = (uint8_t)f;
                    list[n].vendor = (uint16_t)(id & 0xFFFF);
                    list[n].device = (uint16_t)(id >> 16);
                    list[n].class_code = (uint8_t)(cls >> 24);
                    list[n].subclass = (uint8_t)(cls >> 16);
                }
                n++;
            }
        }
    }
    return n < max ? n : max;
}

bool pci_find(uint16_t vendor, uint16_t device, pci_dev_t *out)
{
    pci_dev_t list[32];
    int n = pci_scan(list, 32);
    for (int i = 0; i < n; i++) {
        if (list[i].vendor == vendor && list[i].device == device) {
            *out = list[i];
            return true;
        }
    }
    return false;
}
