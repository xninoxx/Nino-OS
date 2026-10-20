/* rtl8139.c - RTL8139 driver. No interrupts: the network code polls for frames.
 * The kernel has no paging, so static buffers can be handed to the card directly. */
#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "string.h"

#define REG_IDR0     0x00
#define REG_TSD0     0x10
#define REG_TSAD0    0x20
#define REG_RBSTART  0x30
#define REG_CMD      0x37
#define REG_CAPR     0x38
#define REG_IMR      0x3C
#define REG_RCR      0x44
#define REG_CONFIG1  0x52

#define CMD_RESET    0x10
#define CMD_RE       0x08
#define CMD_TE       0x04
#define CMD_BUFE     0x01

#define RX_RING      65536                    /* RCR length code 3 = 64 KiB + 16 */
#define RX_BUF_SIZE  (RX_RING + 16 + 1536)    /* + room for a packet that wraps (WRAP bit) */
#define TX_BUF_SIZE  1792
#define TSD_OWN      0x2000

static uint8_t  rx_buf[RX_BUF_SIZE] __attribute__((aligned(4096)));
static uint8_t  tx_buf[4][TX_BUF_SIZE] __attribute__((aligned(4)));
static uint16_t io;
static uint32_t rx_off;
static int      tx_cur;
static bool     ready;
static uint32_t resets;

bool rtl8139_init(uint8_t mac[6])
{
    pci_dev_t dev;
    if (!pci_find(0x10EC, 0x8139, &dev)) {
        return false;
    }

    uint32_t bar0 = pci_read32(dev.bus, dev.slot, dev.func, 0x10);
    if (!(bar0 & 1)) {
        return false;                                     /* we need the I/O BAR */
    }
    io = (uint16_t)(bar0 & 0xFFFC);

    uint32_t cmd = pci_read32(dev.bus, dev.slot, dev.func, 0x04);
    pci_write32(dev.bus, dev.slot, dev.func, 0x04, cmd | 0x07);   /* I/O space, memory, bus master (DMA) */

    outb(io + REG_CONFIG1, 0x00);                         /* power on */
    outb(io + REG_CMD, CMD_RESET);
    for (uint32_t t = 1000000; t && (inb(io + REG_CMD) & CMD_RESET); t--) { }

    for (int i = 0; i < 6; i++) {
        mac[i] = inb(io + REG_IDR0 + i);
    }

    memset(rx_buf, 0, sizeof(rx_buf));
    rx_off = 0;
    tx_cur = 0;
    outl(io + REG_RBSTART, (uint32_t)(uintptr_t)rx_buf);
    outw(io + REG_IMR, 0x0000);                           /* polling: no interrupts */
    /* accept all/physical/multicast/broadcast, WRAP, 64 KiB ring */
    outl(io + REG_RCR, 0x0F | (1u << 7) | (3u << 11));
    outb(io + REG_CMD, CMD_RE | CMD_TE);

    ready = true;
    return true;
}

void rtl8139_poll(void (*handler)(const uint8_t *frame, uint32_t len))
{
    if (!ready) {
        return;
    }
    while (!(inb(io + REG_CMD) & CMD_BUFE)) {
        const uint8_t *hdr = rx_buf + rx_off;
        uint16_t status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        uint16_t len    = (uint16_t)(hdr[2] | (hdr[3] << 8));   /* includes the 4-byte CRC */

        if (!(status & 0x0001) || len < 18 || len > 1600) {
            /* Bad frame or lost sync: skip everything that is in the ring */
            resets++;
            outb(io + REG_CMD, CMD_TE);
            outb(io + REG_CMD, CMD_RE | CMD_TE);
            rx_off = 0;
            outw(io + REG_CAPR, (uint16_t)(0 - 16));
            outl(io + REG_RBSTART, (uint32_t)(uintptr_t)rx_buf);
            outl(io + REG_RCR, 0x0F | (1u << 7) | (3u << 11));
            return;
        }
        handler(hdr + 4, (uint32_t)len - 4);

        rx_off = (rx_off + len + 4 + 3) & ~3u;
        rx_off %= RX_RING;
        outw(io + REG_CAPR, (uint16_t)(rx_off - 16));
    }
}

uint32_t rtl8139_resets(void)
{
    return resets;
}

bool rtl8139_send(const uint8_t *frame, uint32_t len)
{
    if (!ready || len > 1514) {
        return false;
    }
    int d = tx_cur;
    for (uint32_t t = 1000000; t && !(inl(io + REG_TSD0 + 4 * d) & TSD_OWN); t--) { }

    memcpy(tx_buf[d], frame, len);
    if (len < 60) {
        memset(tx_buf[d] + len, 0, 60 - len);
        len = 60;
    }
    outl(io + REG_TSAD0 + 4 * d, (uint32_t)(uintptr_t)tx_buf[d]);
    outl(io + REG_TSD0 + 4 * d, len);                     /* writing the size starts the transmit */
    tx_cur = (tx_cur + 1) & 3;
    return true;
}
