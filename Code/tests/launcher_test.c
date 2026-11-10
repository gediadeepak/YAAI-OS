#include "../edit_keys.h"
#include "../settings.h"
#include "../shell_ui.h"
#define CHECK(x)                                                                                           \
    do {                                                                                                   \
        if (!(x))                                                                                          \
            return __LINE__;                                                                               \
    } while (0)
static uint32_t pixels[640 * 480 + 2];
static void type(ShellLauncher *s, const char *text) {
    while (*text)
        shell_launcher_key(s, *text++);
}
int launcher_test(void) {
    ShellLauncher s;
    shell_launcher_init(&s);
    CHECK(shell_launcher_count(&s) == 6);
    CHECK(shell_launcher_key(&s, 's') == 0);
    CHECK(shell_launcher_key(&s, 'r') == 0);
    CHECK(s.query[0] == 's' && s.query[1] == 'r');
    shell_launcher_init(&s);
    type(&s, "SETTINGS");
    CHECK(shell_launcher_count(&s) == 1);
    CHECK(shell_launcher_key(&s, '\n') == 3);
    shell_launcher_key(&s, (char)EDIT_SELECT_ALL);
    type(&s, "calculator");
    CHECK(shell_launcher_count(&s) == 1 && shell_launcher_key(&s, '\n') == 15);
    CHECK(shell_launcher_key(&s, 27) == 0 && !s.query[0]);
    CHECK(shell_launcher_key(&s, 27) == SHELL_CLOSE);
    type(&s, "zzzzzz");
    CHECK(shell_launcher_count(&s) == 0);
    CHECK(shell_launcher_key(&s, '\n') == 0);
    for (unsigned i = 0; i < 20; ++i) {
        shell_launcher_key(&s, (char)EDIT_UP);
        CHECK(s.selected >= 0 && s.selected < 2);
    }
    shell_launcher_key(&s, (char)EDIT_END);
    CHECK(shell_launcher_key(&s, '\n') == 14);
    shell_launcher_key(&s, (char)EDIT_PAGE_DOWN);
    CHECK(shell_launcher_key(&s, '\n') == 13);
    shell_launcher_init(&s);
    for (unsigned i = 0; i < 100; ++i)
        shell_launcher_key(&s, 'x');
    CHECK(s.query[SHELL_QUERY - 1] == 0);
    shell_launcher_key(&s, (char)EDIT_SELECT_ALL);
    shell_launcher_key(&s, '\b');
    CHECK(!s.query[0]);
    unsigned widths[] = {640, 800, 1920}, heights[] = {480, 600, 1080};
    for (unsigned n = 0; n < 3; ++n) {
        GuiRect dock = {(int)(widths[n] - 472) / 2, (int)heights[n] - 104, 472, 84};
        GuiRect b = shell_launcher_bounds(widths[n], heights[n], dock);
        CHECK(b.x >= 0 && b.y >= 0 && b.x + b.width <= (int)widths[n]);
        CHECK(b.y + b.height < dock.y);
        for (unsigned i = 0; i < 6; ++i) {
            GuiRect t = shell_launcher_tile(b, i);
            const ShellApp *a = shell_app(i);
            CHECK(t.y + t.height <= b.y + b.height - 76);
            CHECK(t.x >= b.x && t.x + t.width <= b.x + b.width);
            CHECK(shell_launcher_hit(&s, b, t.x + t.width / 2, t.y + t.height / 2) == a->target);
            shell_launcher_hover(&s, b, t.x + 8, t.y + 8);
            CHECK(s.selected == (int)i);
        }
        CHECK(shell_launcher_hit(&s, b, b.x + 40, b.y + b.height - 40) == 13);
        CHECK(shell_launcher_hit(&s, b, b.x + b.width - 40, b.y + b.height - 40) == 14);
        CHECK(shell_launcher_hit(&s, b, -1, -1) == 0);
    }
    CHECK(gui_initialize());
    BootInfo screen = {0};
    screen.width = 640;
    screen.height = 480;
    screen.pixels_per_scanline = 640;
    screen.pixel_format = 1;
    screen.framebuffer = pixels + 1;
    pixels[0] = 0x12345678;
    pixels[640 * 480 + 1] = 0xabcdef01;
    GuiRect b = shell_launcher_bounds(640, 480, (GuiRect){84, 376, 472, 84});
    unsigned old = settings_current()->color_mode;
    for (unsigned mode = 0; mode < 3; ++mode) {
        settings_color_mode(mode);
        shell_launcher_init(&s);
        shell_launcher_draw(&screen, &s, b);
        type(&s, "no results");
        shell_launcher_draw(&screen, &s, b);
        for (unsigned i = 0; i < 17; ++i)
            shell_icon(&screen, i, (GuiRect){10 + (int)i * 35, 420, 30, 30});
        shell_tooltip(&screen, (GuiRect){0, 400, 56, 56}, "About YAAI OS");
        CHECK(pixels[0] == 0x12345678 && pixels[640 * 480 + 1] == 0xabcdef01);
    }
    settings_color_mode(old);
    return 0;
}