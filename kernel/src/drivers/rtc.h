#pragma once
#include <stdint.h>

typedef struct {
    uint8_t  sec;    /* 0-59  */
    uint8_t  min;    /* 0-59  */
    uint8_t  hour;   /* 0-23  */
    uint8_t  day;    /* 1-31  */
    uint8_t  month;  /* 1-12  */
    uint16_t year;   /* 2000+ */
} rtc_time_t;

/* Read the current time from the CMOS RTC. */
void rtc_read(rtc_time_t *out);

/* Returns day-of-week string for a given year/month/day (Zeller). */
const char *rtc_weekday(uint16_t y, uint8_t m, uint8_t d);
