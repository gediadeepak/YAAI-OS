#include "../desktop_keys.h"
#include "../edit_keys.h"
#include "../quick_settings.h"
#include "../settings.h"
#define CHECK(x)                                                                                           \
    do {                                                                                                   \
        if (!(x))                                                                                          \
            return __LINE__;                                                                               \
    } while (0)
static uint32_t pixels[640 * 480 + 2];
static int overlap(GuiRect a, GuiRect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
int quick_settings_test(void) {
    CHECK(desktop_shortcut(16, 0) == DESKTOP_QUICK);
    CHECK(quick_calendar_days(2000, 2) == 29 && quick_calendar_days(2100, 2) == 28);
    CHECK(quick_calendar_days(2024, 2) == 29 && quick_calendar_days(2026, 2) == 28);
    CHECK(quick_calendar_first(2026, 11) == 0 && quick_calendar_first(2000, 2) == 2);
    CHECK(!quick_calendar_days(2026, 0) && quick_calendar_first(2026, 13) == -1);
    QuickInfo info = {
        {2026, 11, 10, 12, 34, 0},      1, 1, "Network: Ethernet / DHCP configured", "10.0.2.15",
        "Wi-Fi: use VM host networking"};
    QuickSettings s;
    quick_settings_open(&s, &info);
    CHECK(s.month == 11 && s.year == 2026);
    unsigned old_volume = settings_current()->volume, old_mode = settings_current()->color_mode;
    settings_volume(75);
    quick_settings_key(&s, &info, 'm');
    CHECK(!settings_current()->volume);
    quick_settings_key(&s, &info, 'm');
    CHECK(settings_current()->volume == 75);
    s.focus = 1;
    quick_settings_key(&s, &info, (char)EDIT_RIGHT);
    CHECK(settings_current()->volume == 80);
    info.audio = 0;
    quick_settings_key(&s, &info, 'm');
    CHECK(settings_current()->volume == 80);
    quick_settings_key(&s, &info, (char)EDIT_LEFT);
    CHECK(settings_current()->volume == 80);
    info.audio = 1;
    unsigned widths[] = {360, 480, 640, 800, 1280, 1920}, heights[] = {640, 800, 480, 600, 720, 1080};
    for (unsigned n = 0; n < 6; ++n) {
        GuiRect b = quick_settings_bounds(widths[n], heights[n]);
        CHECK(b.x >= 0 && b.y >= 0 && b.x + b.width <= (int)widths[n] && b.y + b.height <= (int)heights[n]);
        for (unsigned k = 0; k < QUICK_CONTROLS; ++k) {
            GuiRect r = quick_settings_control(b, k);
            CHECK(r.width >= 44 && r.height >= 44);
            CHECK(r.x >= b.x && r.y >= b.y && r.x + r.width <= b.x + b.width &&
                  r.y + r.height <= b.y + b.height);
            CHECK(quick_settings_hit(b, r.x + r.width / 2, r.y + r.height / 2) == (int)k);
            for (unsigned j = k + 1; j < QUICK_CONTROLS; ++j)
                CHECK(!overlap(r, quick_settings_control(b, j)));
        }
        CHECK(quick_settings_hit(b, -1, -1) == -1);
        GuiRect slider = quick_settings_control(b, 1);
        quick_settings_click(&s, b, &info, slider.x, slider.y + 22);
        CHECK(s.dragging && s.preview == 0 && settings_current()->volume == 80);
        quick_settings_pointer(&s, b, &info, slider.x + slider.width + 100, slider.y, 1);
        CHECK(s.preview == 100 && settings_current()->volume == 80);
        quick_settings_pointer(&s, b, &info, slider.x + slider.width, slider.y, 0);
        CHECK(!s.dragging && settings_current()->volume == 100);
        settings_volume(80);
        quick_settings_click(&s, b, &info, slider.x, slider.y + 22);
        quick_settings_cancel(&s);
        CHECK(!s.dragging && settings_current()->volume == 80);
    }
    s.year = 2026;
    s.month = 12;
    s.focus = 7;
    quick_settings_key(&s, &info, '\n');
    CHECK(s.year == 2027 && s.month == 1);
    s.focus = 5;
    quick_settings_key(&s, &info, '\n');
    CHECK(s.year == 2026 && s.month == 12);
    s.year = 1970;
    s.month = 1;
    quick_settings_key(&s, &info, '\n');
    CHECK(s.year == 1970 && s.month == 1);
    s.focus = 6;
    quick_settings_key(&s, &info, '\n');
    CHECK(s.year == 2026 && s.month == 11);
    for (unsigned n = 0; n < 3; ++n) {
        s.focus = (int)n + 2;
        quick_settings_key(&s, &info, '\n');
        CHECK(settings_current()->color_mode == n);
    }
    s.focus = 9;
    CHECK(quick_settings_key(&s, &info, '\n') == QUICK_RESTART);
    s.focus = 10;
    CHECK(quick_settings_key(&s, &info, '\n') == QUICK_SHUTDOWN);
    s.focus = 8;
    CHECK(quick_settings_key(&s, &info, '\n') == QUICK_SETTINGS);
    CHECK(quick_settings_key(&s, &info, 27) == QUICK_CLOSE);
    for (unsigned n = 0; n < 30; ++n) {
        quick_settings_key(&s, &info, '\t');
        CHECK(s.focus >= 0 && s.focus < QUICK_CONTROLS);
    }
    CHECK(gui_initialize());
    BootInfo canvas = {0};
    canvas.width = 640;
    canvas.height = 480;
    canvas.pixels_per_scanline = 640;
    canvas.pixel_format = 1;
    canvas.framebuffer = pixels + 1;
    pixels[0] = 0x12345678;
    pixels[640 * 480 + 1] = 0xabcdef01;
    for (unsigned n = 0; n < 3; ++n) {
        settings_color_mode(n);
        quick_settings_draw(&canvas, &s, &info, quick_settings_bounds(640, 480));
        canvas.width = 360;
        canvas.height = 640;
        canvas.pixels_per_scanline = 360;
        quick_settings_draw(&canvas, &s, &info, quick_settings_bounds(360, 640));
        canvas.width = 640;
        canvas.height = 480;
        canvas.pixels_per_scanline = 640;
        CHECK(pixels[0] == 0x12345678 && pixels[640 * 480 + 1] == 0xabcdef01);
    }
    info.clock_ready = info.audio = 0;
    quick_settings_open(&s, &info);
    quick_settings_draw(&canvas, &s, &info, quick_settings_bounds(640, 480));
    CHECK(!s.year && !s.month && quick_settings_key(&s, &info, 'r') == 0 &&
          quick_settings_key(&s, &info, 's') == 0);
    settings_volume(old_volume);
    settings_color_mode(old_mode);
    return 0;
}