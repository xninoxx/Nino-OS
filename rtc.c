/* rtc.c - read the time from the CMOS real-time clock */
#include "rtc.h"
#include "io.h"

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

void rtc_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    /* Wait (bounded) until no update is in progress */
    for (uint32_t t = 0; t < 100000 && (cmos_read(0x0A) & 0x80); t++) { }

    uint8_t s = cmos_read(0x00);
    uint8_t m = cmos_read(0x02);
    uint8_t h = cmos_read(0x04);
    uint8_t status_b = cmos_read(0x0B);

    uint8_t pm = h & 0x80;                 /* PM flag in 12-hour mode */
    h &= 0x7F;

    if (!(status_b & 0x04)) {              /* values are BCD */
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin(h);
    }
    if (!(status_b & 0x02)) {              /* 12-hour mode */
        h %= 12;
        if (pm) {
            h += 12;
        }
    }

    *hour = h;
    *minute = m;
    *second = s;
}

void rtc_get_date(uint16_t *year, uint8_t *month, uint8_t *day)
{
    for (uint32_t t = 0; t < 100000 && (cmos_read(0x0A) & 0x80); t++) { }

    uint8_t d = cmos_read(0x07);
    uint8_t mo = cmos_read(0x08);
    uint8_t y = cmos_read(0x09);
    uint8_t status_b = cmos_read(0x0B);

    if (!(status_b & 0x04)) {              /* values are BCD */
        d = bcd_to_bin(d);
        mo = bcd_to_bin(mo);
        y = bcd_to_bin(y);
    }
    *year = (uint16_t)(2000 + y);          /* the two-digit year is enough until 2099 */
    *month = mo;
    *day = d;
}


uint32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + (int)doe - 719468);
}

uint32_t rtc_unix_time(void)
{
    uint16_t y;
    uint8_t mo, d, h, mi, s;
    rtc_get_date(&y, &mo, &d);
    rtc_get_time(&h, &mi, &s);
    return days_from_civil(y, mo, d) * 86400u + (uint32_t)h * 3600u + (uint32_t)mi * 60u + s;
}
