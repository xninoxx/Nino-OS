/* rtc.h - CMOS real-time clock */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

void rtc_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second);
uint32_t rtc_unix_time(void);                  /* seconds since 1970 (the RTC is read as UTC) */
uint32_t days_from_civil(int y, int m, int d);  /* days since 1970-01-01 */
void rtc_get_date(uint16_t *year, uint8_t *month, uint8_t *day);

#endif /* RTC_H */