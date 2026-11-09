#include "platform/platform.h"
#include "clock.h"
#include "console.h"
#include "setup.h"

static int month_days(int year, int month)
{
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int weekday(int year, int month, int day)
{
    static const int offsets[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

static int offset_minutes(uint32_t zone, const DateTime *utc)
{
    if (zone == 1) return 330;
    if (zone == 2) {
        int march = 31 - weekday(utc->year, 3, 31);
        int october = 31 - weekday(utc->year, 10, 31);
        int summer = (utc->month > 3 && utc->month < 10) ||
            (utc->month == 3 && (utc->day > march || (utc->day == march && utc->hour >= 1))) ||
            (utc->month == 10 && (utc->day < october || (utc->day == october && utc->hour < 1)));
        return summer ? 60 : 0;
    }
    if (zone >= 3 && zone <= 6) {
        int base = -300 - (int)(zone - 3) * 60;
        int march = 8 + (7 - weekday(utc->year, 3, 1)) % 7;
        int november = 1 + (7 - weekday(utc->year, 11, 1)) % 7;
        int summer = utc->year >= 2007 && ((utc->month > 3 && utc->month < 11) ||
            (utc->month == 3 && (utc->day > march || (utc->day == march && utc->hour >= 2 - base / 60))) ||
            (utc->month == 11 && (utc->day < november || (utc->day == november && utc->hour < 1 - base / 60))));
        return base + (summer ? 60 : 0);
    }
    return 0;
}

int clock_local_time(uint32_t zone, DateTime *time)
{
    if (!platform_clock_utc(time)) return 0;
    if (time->year < 1970 || time->year > 9999 || time->month < 1 || time->month > 12 ||
        time->day < 1 || time->day > month_days(time->year, time->month) ||
        time->hour > 23 || time->minute > 59 || time->second > 59) return 0;
    clock_apply_zone(zone, time);
    return 1;
}

void clock_apply_zone(uint32_t zone, DateTime *time)
{
    int minutes = time->hour * 60 + time->minute + offset_minutes(zone, time);
    if (minutes < 0) {
        minutes += 1440;
        if (--time->day == 0) {
            if (--time->month == 0) { time->month = 12; --time->year; }
            time->day = month_days(time->year, time->month);
        }
    } else if (minutes >= 1440) {
        minutes -= 1440;
        if (++time->day > month_days(time->year, time->month)) {
            time->day = 1;
            if (++time->month > 12) { time->month = 1; ++time->year; }
        }
    }
    time->hour = minutes / 60;
    time->minute = minutes % 60;
}

static void two_digits(int number)
{
    if (number < 10) console_write("0");
    console_decimal((uint64_t)number);
}

void clock_print(uint32_t zone)
{
    DateTime time;
    console_write("COUNTRY / ZONE  "); console_write(setup_zone_name(zone)); console_write("\n");
    if (!clock_local_time(zone, &time)) { console_write("CLOCK UNAVAILABLE\n"); return; }
    console_write("LOCAL DATE/TIME "); console_decimal((uint64_t)time.year); console_write("-");
    two_digits(time.month); console_write("-"); two_digits(time.day); console_write(" ");
    two_digits(time.hour); console_write(":"); two_digits(time.minute); console_write(":");
    two_digits(time.second); console_write("\n");
}
