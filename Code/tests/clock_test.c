#include "../clock.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int clock_test(void)
{
    DateTime time = {2026,11,7,21,45,0};
    clock_apply_zone(1, &time);
    CHECK(time.year == 2026 && time.month == 11 && time.day == 8 && time.hour == 3 && time.minute == 15);
    time = (DateTime){2026,1,1,2,0,0};
    clock_apply_zone(3, &time);
    CHECK(time.year == 2025 && time.month == 12 && time.day == 31 && time.hour == 21);
    time = (DateTime){2024,2,29,22,0,0};
    clock_apply_zone(1, &time);
    CHECK(time.month == 3 && time.day == 1 && time.hour == 3 && time.minute == 30);
    time = (DateTime){2026,3,29,0,59,0};
    clock_apply_zone(2, &time);
    CHECK(time.hour == 0 && time.minute == 59);
    time = (DateTime){2026,3,29,1,0,0};
    clock_apply_zone(2, &time);
    CHECK(time.hour == 2);
    time = (DateTime){2026,10,25,1,0,0};
    clock_apply_zone(2, &time);
    CHECK(time.hour == 1);
    time = (DateTime){2026,3,8,6,59,0};
    clock_apply_zone(3, &time);
    CHECK(time.hour == 1 && time.minute == 59);
    time = (DateTime){2026,3,8,7,0,0};
    clock_apply_zone(3, &time);
    CHECK(time.hour == 3);
    time = (DateTime){2026,11,1,5,59,0};
    clock_apply_zone(3, &time);
    CHECK(time.hour == 1 && time.minute == 59);
    time = (DateTime){2026,11,1,6,0,0};
    clock_apply_zone(3, &time);
    CHECK(time.hour == 1);
    time = (DateTime){2026,3,8,9,0,0};
    clock_apply_zone(5, &time);
    CHECK(time.hour == 3);
    time = (DateTime){2026,7,1,12,0,0};
    clock_apply_zone(6, &time);
    CHECK(time.hour == 5);
    return 0;
}
