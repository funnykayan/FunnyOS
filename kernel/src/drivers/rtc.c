/* ============================================================
 *  FunnyOS  –  CMOS / RTC driver
 *  Reads time from the RTC hidden in the CMOS chip via
 *  I/O ports 0x70 (address) and 0x71 (data).
 * ============================================================ */

#include "rtc.h"
#include <stdint.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static inline void cmos_out(uint8_t reg) {
    __asm__ volatile ("outb %0, %1" :: "a"(reg), "Nd"((uint16_t)CMOS_ADDR));
}
static inline uint8_t cmos_in(void) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"((uint16_t)CMOS_DATA));
    return v;
}
static uint8_t cmos_read(uint8_t reg) {
    cmos_out(reg);
    return cmos_in();
}

static uint8_t bcd2bin(uint8_t b) {
    return (uint8_t)((b & 0x0F) + ((b >> 4) * 10));
}

void rtc_read(rtc_time_t *out) {
    /* Spin until "Update In Progress" flag clears */
    while (cmos_read(0x0A) & 0x80) {}

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year  = cmos_read(0x09);
    uint8_t regB  = cmos_read(0x0B);
    uint8_t cent  = cmos_read(0x32);   /* CMOS century register (may be 0) */

    /* Convert BCD → binary if Status Register B bit2 is clear */
    if (!(regB & 0x04)) {
        sec   = bcd2bin(sec);
        min   = bcd2bin(min);
        hour  = bcd2bin(hour & 0x7F);
        day   = bcd2bin(day);
        month = bcd2bin(month);
        year  = bcd2bin(year);
        if (cent) cent = bcd2bin(cent);
    }

    /* 12-hour → 24-hour (PM flag is in bit 7 of the raw hour byte) */
    if (!(regB & 0x02)) {
        uint8_t raw_hour = cmos_read(0x04);
        if (!(regB & 0x04)) raw_hour = bcd2bin(raw_hour & 0x7F);
        else                 raw_hour &= 0x7F;
        if (cmos_read(0x04) & 0x80)
            hour = (uint8_t)((hour % 12) + 12);
    }

    uint16_t full_year;
    if (cent && cent >= 19)
        full_year = (uint16_t)(cent * 100 + year);
    else
        full_year = (uint16_t)(2000 + year);   /* assume 21st century */

    out->sec   = sec;
    out->min   = min;
    out->hour  = hour;
    out->day   = day;
    out->month = month;
    out->year  = full_year;
}

const char *rtc_weekday(uint16_t y, uint8_t m, uint8_t d) {
    static const char *days[] = {
        "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
    };
    /* Tomohiko Sakamoto's algorithm */
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    int dow = ((int)y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    if (dow < 0) dow += 7;
    return days[dow];
}
