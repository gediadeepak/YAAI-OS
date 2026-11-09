#include "../platform.h"
static uint8_t cmos(uint8_t index)
{
    uint8_t value;
    __asm__ volatile("outb %0, %1" : : "a"(index), "Nd"((uint16_t)0x70));
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"((uint16_t)0x71));
    return value;
}

static int decode(uint8_t value, int binary)
{
    return binary ? value : (value & 15) + (value >> 4) * 10;
}

int platform_clock_utc(DateTime *time)
{
    static const uint8_t registers[8] = {0,2,4,7,8,9,0x32,0x0b};
    uint8_t first[8], second[8];
    int stable = 0;
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (cmos(0x0a) & 0x80) continue;
        for (unsigned index = 0; index < 8; ++index) first[index] = cmos(registers[index]);
        if (cmos(0x0a) & 0x80) continue;
        for (unsigned index = 0; index < 8; ++index) second[index] = cmos(registers[index]);
        stable = 1;
        for (unsigned index = 0; index < 8; ++index) {
            if (first[index] != second[index]) stable = 0;
        }
        if (stable) break;
    }
    if (!stable) return 0;
    int binary = first[7] & 4;
    time->second = decode(first[0], binary);
    time->minute = decode(first[1], binary);
    time->hour = decode(first[2] & 0x7f, binary);
    if (!(first[7] & 2)) time->hour = time->hour % 12 + ((first[2] & 0x80) ? 12 : 0);
    time->day = decode(first[3], binary);
    time->month = decode(first[4], binary);
    int century = decode(first[6], binary);
    if (century < 19 || century > 99) century = 20;
    time->year = century * 100 + decode(first[5], binary);
    return 1;
}
