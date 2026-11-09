#ifndef CLOCK_H
#define CLOCK_H
#include <stdint.h>

typedef struct { int year, month, day, hour, minute, second; } DateTime;
int clock_local_time(uint32_t zone, DateTime *time);
// Applies supported regional rules to a valid UTC calendar value.
void clock_apply_zone(uint32_t zone, DateTime *time);
void clock_print(uint32_t zone);
#endif
