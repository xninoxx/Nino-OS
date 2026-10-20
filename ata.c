/* ata.c - ATA PIO driver. Polling only (interrupts disabled with nIEN), 28-bit LBA.
 * Probes the primary and secondary IDE channel, master and slave, and uses the first plain ATA disk
 * (CD-ROMs on the other channel are skipped). */
#include "ata.h"
#include "io.h"

/* The register addresses depend on the channel, so they are variables set by ata_init() */
static uint16_t ATA_IO   = 0x1F0;
static uint16_t ATA_CTRL = 0x3F6;
static uint8_t  drive_bit;                    /* 0x00 = master, 0x10 = slave */
#define R_DATA       (ATA_IO + 0)
#define R_COUNT      (ATA_IO + 2)
#define R_LBA0       (ATA_IO + 3)
#define R_LBA1       (ATA_IO + 4)
#define R_LBA2       (ATA_IO + 5)
#define R_DRIVE      (ATA_IO + 6)
#define R_STATUS     (ATA_IO + 7)
#define R_CMD        (ATA_IO + 7)

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80

static bool     present;
static uint32_t total_sectors;

static void delay400ns(void)
{
    for (int i = 0; i < 4; i++) {
        (void)inb(ATA_CTRL);
    }
}

static bool wait_not_busy(void)
{
    for (uint32_t t = 4000000; t; t--) {
        if (!(inb(R_STATUS) & ST_BSY)) {
            return true;
        }
    }
    return false;
}

static bool wait_drq(void)
{
    for (uint32_t t = 4000000; t; t--) {
        uint8_t s = inb(R_STATUS);
        if (s & (ST_ERR | ST_DF)) {
            return false;
        }
        if (!(s & ST_BSY) && (s & ST_DRQ)) {
            return true;
        }
    }
    return false;
}

static void select_lba(uint32_t lba, uint32_t count)
{
    outb(R_DRIVE, (uint8_t)(0xE0 | drive_bit | ((lba >> 24) & 0x0F)));   /* LBA mode */
    outb(R_COUNT, (uint8_t)count);
    outb(R_LBA0, (uint8_t)lba);
    outb(R_LBA1, (uint8_t)(lba >> 8));
    outb(R_LBA2, (uint8_t)(lba >> 16));
}

static bool identify(void)
{
    if (inb(R_STATUS) == 0xFF) {            /* floating bus: no controller/drive */
        return false;
    }
    outb(ATA_CTRL, 0x02);                   /* nIEN: we poll, no interrupts */
    outb(R_DRIVE, (uint8_t)(0xA0 | drive_bit));
    delay400ns();
    outb(R_COUNT, 0);
    outb(R_LBA0, 0);
    outb(R_LBA1, 0);
    outb(R_LBA2, 0);
    outb(R_CMD, 0xEC);                      /* IDENTIFY */
    delay400ns();

    if (inb(R_STATUS) == 0) {
        return false;                       /* no drive */
    }
    if (!wait_not_busy()) {
        return false;
    }
    if (inb(R_LBA1) != 0 || inb(R_LBA2) != 0) {
        return false;                       /* ATAPI (CD-ROM) or SATA signature, not a plain ATA disk */
    }
    if (!wait_drq()) {
        return false;
    }

    uint16_t id[256];
    insw(R_DATA, id, 256);
    total_sectors = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
    return total_sectors > 0;
}

bool ata_init(void)
{
    static const struct { uint16_t io, ctrl; uint8_t drive; } spots[4] = {
        { 0x1F0, 0x3F6, 0x00 }, { 0x1F0, 0x3F6, 0x10 },      /* primary master, slave */
        { 0x170, 0x376, 0x00 }, { 0x170, 0x376, 0x10 }       /* secondary master, slave */
    };
    present = false;
    total_sectors = 0;
    for (int i = 0; i < 4; i++) {
        ATA_IO = spots[i].io;
        ATA_CTRL = spots[i].ctrl;
        drive_bit = spots[i].drive;
        if (identify()) {
            present = true;
            return true;
        }
    }
    ATA_IO = 0x1F0;
    ATA_CTRL = 0x3F6;
    drive_bit = 0;
    return false;
}

bool ata_present(void)
{
    return present;
}

uint32_t ata_sectors(void)
{
    return total_sectors;
}

bool ata_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!present || lba + count > total_sectors) {
        return false;
    }
    uint8_t *p = buf;
    for (uint32_t i = 0; i < count; i++) {
        if (!wait_not_busy()) {
            return false;
        }
        select_lba(lba + i, 1);
        outb(R_CMD, 0x20);                  /* READ SECTORS */
        delay400ns();
        if (!wait_drq()) {
            return false;
        }
        insw(R_DATA, p, 256);
        p += 512;
    }
    return true;
}

bool ata_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (!present || lba + count > total_sectors) {
        return false;
    }
    const uint8_t *p = buf;
    for (uint32_t i = 0; i < count; i++) {
        if (!wait_not_busy()) {
            return false;
        }
        select_lba(lba + i, 1);
        outb(R_CMD, 0x30);                  /* WRITE SECTORS */
        delay400ns();
        if (!wait_drq()) {
            return false;
        }
        outsw(R_DATA, p, 256);
        p += 512;
        if (!wait_not_busy()) {
            return false;
        }
    }
    outb(R_CMD, 0xE7);                      /* FLUSH CACHE */
    return wait_not_busy();
}
