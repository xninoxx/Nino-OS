/* nic.c - network card dispatch */
#include "nic.h"
#include "e1000.h"
#include "rtl8139.h"

static enum { NIC_NONE, NIC_E1000, NIC_RTL8139 } kind;

bool nic_init(uint8_t mac[6])
{
    if (e1000_init(mac)) {
        kind = NIC_E1000;
        return true;
    }
    if (rtl8139_init(mac)) {
        kind = NIC_RTL8139;
        return true;
    }
    kind = NIC_NONE;
    return false;
}

void nic_poll(void (*handler)(const uint8_t *frame, uint32_t len))
{
    if (kind == NIC_E1000) e1000_poll(handler);
    else if (kind == NIC_RTL8139) rtl8139_poll(handler);
}

bool nic_send(const uint8_t *frame, uint32_t len)
{
    if (kind == NIC_E1000) return e1000_send(frame, len);
    if (kind == NIC_RTL8139) return rtl8139_send(frame, len);
    return false;
}

const char *nic_name(void)
{
    return kind == NIC_E1000 ? "Intel e1000" : kind == NIC_RTL8139 ? "Realtek RTL8139" : "none";
}

uint32_t nic_resets(void)
{
    return kind == NIC_RTL8139 ? rtl8139_resets() : 0;
}
