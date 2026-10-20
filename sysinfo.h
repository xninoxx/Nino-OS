/* sysinfo.h - system facts collected at boot (defined in kernel.c) */
#ifndef SYSINFO_H
#define SYSINFO_H

#include <stdint.h>

struct sysinfo {
    uint32_t ram_kb;       /* KB of RAM above 1 MB, from the Multiboot info */
    int      screen_w;
    int      screen_h;
};

extern struct sysinfo g_sysinfo;

#endif /* SYSINFO_H */