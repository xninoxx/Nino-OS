/* sysreport.c - system facts: CPU, RAM, screen, disk, network, keyboard */
#include "sysreport.h"
#include "sysinfo.h"
#include "persist.h"
#include "net.h"
#include "nic.h"
#include "tls.h"
#include "timer.h"
#include "keyboard.h"
#include "util.h"
#include "string.h"

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

void sysreport_cpu(char *out, int size)
{
    uint32_t a, b, c, d;
    cpuid(0x80000000u, &a, &b, &c, &d);
    char brand[49];
    brand[0] = '\0';
    if (a >= 0x80000004u) {
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002u + i, &a, &b, &c, &d);
            memcpy(brand + 16 * i, &a, 4);
            memcpy(brand + 16 * i + 4, &b, 4);
            memcpy(brand + 16 * i + 8, &c, 4);
            memcpy(brand + 16 * i + 12, &d, 4);
        }
        brand[48] = '\0';
    } else {
        cpuid(0, &a, &b, &c, &d);                            /* vendor string only */
        memcpy(brand, &b, 4);
        memcpy(brand + 4, &d, 4);
        memcpy(brand + 8, &c, 4);
        brand[12] = '\0';
    }
    const char *s = brand;
    while (*s == ' ') s++;
    int i = 0;
    while (s[i] && i < size - 1) { out[i] = s[i]; i++; }
    out[i] = '\0';
}

void sysreport_text(char *out, int size)
{
    (void)size;                                              /* the report is always far below 1 KB */
    char *p = out;
    char tmp[64];

    p = put_str(p, "NinoOS 0.7 bug report\n");
    sysreport_cpu(tmp, sizeof(tmp));
    p = put_str(p, "CPU: "); p = put_str(p, tmp); p = put_str(p, "\n");
    p = put_str(p, "RAM: "); p = put_uint(p, g_sysinfo.ram_kb / 1024 + 1); p = put_str(p, " MB\n");
    p = put_str(p, "Screen: "); p = put_uint(p, (uint32_t)g_sysinfo.screen_w);
    p = put_str(p, "x"); p = put_uint(p, (uint32_t)g_sysinfo.screen_h); p = put_str(p, "\n");
    p = put_str(p, "Disk: ");
    if (persist_present()) { p = put_uint(p, persist_disk_sectors() / 2048); p = put_str(p, " MiB (files are saved)\n"); }
    else p = put_str(p, "none (files are lost on reboot)\n");
    p = put_str(p, "Network: ");
    p = put_str(p, nic_name());
    if (net_up()) {
        char ip[16];
        p = put_str(p, ", IP "); p = put_str(p, net_ip_str(ip, net_ip()));
        p = put_str(p, net_used_dhcp() ? " (DHCP)" : " (default address, DHCP failed)");
    } else {
        p = put_str(p, ", not started");
    }
    p = put_str(p, "\n");
    p = put_str(p, tls_status()); p = put_str(p, "\n");
    p = put_str(p, "Keyboard: "); p = put_str(p, keyboard_layout()); p = put_str(p, "\n");
    p = put_str(p, "Uptime: "); p = put_uptime(p, timer_ticks() / TIMER_HZ); p = put_str(p, "\n");
}
