/* e1000.c - Intel PRO/1000 driver (polling, no interrupts). The registers are memory-mapped (BAR0);
 * the kernel has no paging, so the physical address can be used directly. */
#include "e1000.h"
#include "pci.h"
#include "string.h"

#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_EERD    0x0014
#define REG_ICR     0x00C0
#define REG_IMC     0x00D8
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410
#define REG_RDBAL   0x2800
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_TDBAL   0x3800
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_MTA     0x5200
#define REG_RAL0    0x5400
#define REG_RAH0    0x5404

#define CTRL_SLU    (1u << 6)
#define CTRL_RST    (1u << 26)
#define RCTL_EN     (1u << 1)
#define RCTL_MPE    (1u << 4)
#define RCTL_BAM    (1u << 15)
#define RCTL_SECRC  (1u << 26)
#define TCTL_EN     (1u << 1)
#define TCTL_PSP    (1u << 3)

#define RX_COUNT    32
#define TX_COUNT    16
#define BUF_SIZE    2048

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) tx_desc_t;

static volatile rx_desc_t rx_ring[RX_COUNT] __attribute__((aligned(128)));
static volatile tx_desc_t tx_ring[TX_COUNT] __attribute__((aligned(128)));
static uint8_t rx_buf[RX_COUNT][BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buf[TX_COUNT][BUF_SIZE] __attribute__((aligned(16)));

static volatile uint8_t *mmio;
static uint32_t rx_cur, tx_cur;
static bool ready;

static inline uint32_t rd(uint32_t reg) { return *(volatile uint32_t *)(mmio + reg); }
static inline void wr(uint32_t reg, uint32_t v) { *(volatile uint32_t *)(mmio + reg) = v; }

static uint16_t eeprom_read(uint8_t addr)
{
    wr(REG_EERD, 1u | ((uint32_t)addr << 8));
    for (uint32_t t = 100000; t; t--) {
        uint32_t v = rd(REG_EERD);
        if (v & (1u << 4)) {
            return (uint16_t)(v >> 16);
        }
    }
    return 0;
}

bool e1000_init(uint8_t mac[6])
{
    static const uint16_t ids[] = { 0x100E, 0x100F, 0x1004, 0x1008, 0x100C, 0x1015, 0x1019 };
    pci_dev_t dev;
    bool found = false;
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        if (pci_find(0x8086, ids[i], &dev)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    uint32_t bar0 = pci_read32(dev.bus, dev.slot, dev.func, 0x10);
    if (bar0 & 1) {
        return false;                                        /* an I/O BAR: not what we expect */
    }
    mmio = (volatile uint8_t *)(uintptr_t)(bar0 & 0xFFFFFFF0u);

    uint32_t cmd = pci_read32(dev.bus, dev.slot, dev.func, 0x04);
    pci_write32(dev.bus, dev.slot, dev.func, 0x04, cmd | 0x07);   /* I/O, memory, bus master (DMA) */

    wr(REG_IMC, 0xFFFFFFFFu);                                /* no interrupts: we poll */
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);                   /* reset the card */
    for (uint32_t t = 1000000; t && (rd(REG_CTRL) & CTRL_RST); t--) { }
    wr(REG_IMC, 0xFFFFFFFFu);
    (void)rd(REG_ICR);
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_SLU);                   /* set link up */

    /* MAC address: from the receive-address registers, else from the EEPROM */
    uint32_t ral = rd(REG_RAL0), rah = rd(REG_RAH0);
    if (ral == 0 && (rah & 0xFFFF) == 0) {
        uint16_t w0 = eeprom_read(0), w1 = eeprom_read(1), w2 = eeprom_read(2);
        mac[0] = (uint8_t)w0; mac[1] = (uint8_t)(w0 >> 8);
        mac[2] = (uint8_t)w1; mac[3] = (uint8_t)(w1 >> 8);
        mac[4] = (uint8_t)w2; mac[5] = (uint8_t)(w2 >> 8);
        wr(REG_RAL0, (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24));
        wr(REG_RAH0, (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | (1u << 31));
    } else {
        mac[0] = (uint8_t)ral; mac[1] = (uint8_t)(ral >> 8);
        mac[2] = (uint8_t)(ral >> 16); mac[3] = (uint8_t)(ral >> 24);
        mac[4] = (uint8_t)rah; mac[5] = (uint8_t)(rah >> 8);
    }

    for (int i = 0; i < 128; i++) {
        wr(REG_MTA + 4u * (uint32_t)i, 0);                   /* clear the multicast table */
    }

    /* Receive ring */
    memset((void *)rx_ring, 0, sizeof(rx_ring));
    for (int i = 0; i < RX_COUNT; i++) {
        rx_ring[i].addr = (uint64_t)(uintptr_t)rx_buf[i];
    }
    wr(REG_RDBAL, (uint32_t)(uintptr_t)rx_ring);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, RX_COUNT * sizeof(rx_desc_t));
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_COUNT - 1);
    rx_cur = 0;
    wr(REG_RCTL, RCTL_EN | RCTL_MPE | RCTL_BAM | RCTL_SECRC);   /* 2048-byte buffers, strip the CRC */

    /* Transmit ring */
    memset((void *)tx_ring, 0, sizeof(tx_ring));
    for (int i = 0; i < TX_COUNT; i++) {
        tx_ring[i].addr = (uint64_t)(uintptr_t)tx_buf[i];
        tx_ring[i].status = 1;                               /* "done": free to use */
    }
    wr(REG_TDBAL, (uint32_t)(uintptr_t)tx_ring);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, TX_COUNT * sizeof(tx_desc_t));
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    tx_cur = 0;
    wr(REG_TIPG, 0x0060200Au);
    wr(REG_TCTL, TCTL_EN | TCTL_PSP | (0x10u << 4) | (0x40u << 12));

    ready = true;
    return true;
}

void e1000_poll(void (*handler)(const uint8_t *frame, uint32_t len))
{
    if (!ready) {
        return;
    }
    for (int guard = 0; guard < RX_COUNT; guard++) {
        volatile rx_desc_t *d = &rx_ring[rx_cur];
        if (!(d->status & 0x01)) {
            break;                                           /* no more received frames */
        }
        uint32_t len = d->length;
        if ((d->status & 0x02) && d->errors == 0 && len >= 14 && len <= BUF_SIZE) {
            handler(rx_buf[rx_cur], len);
        }
        d->status = 0;
        wr(REG_RDT, rx_cur);                                 /* give the descriptor back to the card */
        rx_cur = (rx_cur + 1) % RX_COUNT;
    }
}

bool e1000_send(const uint8_t *frame, uint32_t len)
{
    if (!ready || len > 1514) {
        return false;
    }
    volatile tx_desc_t *d = &tx_ring[tx_cur];
    for (uint32_t t = 1000000; t && !(d->status & 0x01); t--) { }   /* wait until the slot is free */

    memcpy(tx_buf[tx_cur], frame, len);
    if (len < 60) {
        memset(tx_buf[tx_cur] + len, 0, 60 - len);
        len = 60;
    }
    d->length = (uint16_t)len;
    d->cso = 0;
    d->css = 0;
    d->special = 0;
    d->status = 0;
    d->cmd = 0x0B;                                           /* EOP | IFCS | RS */
    tx_cur = (tx_cur + 1) % TX_COUNT;
    wr(REG_TDT, tx_cur);
    return true;
}
