/* ata.h - ATA PIO disk driver (primary bus, master drive, LBA28, polling) */
#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stdbool.h>

bool     ata_init(void);                                    /* true if a disk was found */
bool     ata_present(void);
uint32_t ata_sectors(void);                                 /* disk size in 512-byte sectors */
bool     ata_read(uint32_t lba, uint32_t count, void *buf);
bool     ata_write(uint32_t lba, uint32_t count, const void *buf);

#endif /* ATA_H */
