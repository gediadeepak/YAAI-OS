#include "../apps.h"
#include "../edit_keys.h"
#include "../settings_app.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static uint32_t pixels[640 * 480 + 2];
int settings_ui_test(void) {
    CHECK(gui_initialize());
    pixels[0] = 0x12345678;
    pixels[640 * 480 + 1] = 0xabcdef01;
    BootInfo canvas = {0};
    canvas.framebuffer = pixels + 1;
    canvas.width = 640;
    canvas.height = 480;
    canvas.pixels_per_scanline = 640;
    canvas.pixel_format = 1;
    GuiRect w = {0, 0, 640, 480};
    settings_app_select(6);
    settings_application.draw(&canvas, w);
    settings_application.key((char)EDIT_PAGE_DOWN);
    CHECK(settings_app_scroll() > 0);
    settings_application.draw(&canvas, w);
    CHECK(pixels[0] == 0x12345678 && pixels[640 * 480 + 1] == 0xabcdef01);
    settings_application.key((char)EDIT_HOME);
    CHECK(!settings_app_scroll());
    settings_application.click(w, 220, 84);
    const char *query = "WALLPAPER";
    while (*query)
        settings_application.key(*query++);
    settings_application.draw(&canvas, w);
    settings_application.click(w, 260, 207);
    CHECK(settings_app_page() == 1);
    settings_application.draw(&canvas, w);
    settings_application.click(w, 70, 160);
    CHECK(settings_app_page() == 6);
    settings_application.draw(&canvas, w);
    settings_application.click(w, 270, 382);
    CHECK(settings_app_page() == 0);
    settings_application.draw(&canvas, w);
    settings_application.click(w, 220, 84);
    query = "nothing matches this";
    while (*query)
        settings_application.key(*query++);
    settings_application.draw(&canvas, w);
    settings_application.click(w, 260, 207);
    CHECK(settings_app_page() == 0);
    CHECK(settings_application.escape());
    settings_application.draw(&canvas, w);
    settings_application.key((char)EDIT_END);
    CHECK(settings_app_scroll() > 0);
    settings_application.draw(&canvas, w);
    settings_application.draw(&canvas, (GuiRect){20, 30, 600, 440});
    CHECK(pixels[0] == 0x12345678 && pixels[640 * 480 + 1] == 0xabcdef01);
    settings_application.click(w, 220, 84);
    settings_application.key((char)EDIT_SELECT_ALL);
    query = "sound";
    while (*query)
        settings_application.key(*query++);
    settings_application.key('\n');
    CHECK(settings_app_page() == 2);
    settings_app_select(6);
    return 0;
}
