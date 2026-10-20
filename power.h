/* power.h - shutdown and reboot */
#ifndef POWER_H
#define POWER_H

void power_off(void) __attribute__((noreturn));   /* saves files, then asks the VM / PC to power off */
void power_reboot(void) __attribute__((noreturn));

#endif /* POWER_H */
