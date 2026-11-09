#include "../gui.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int gui_test(void)
{
    CHECK(gui_initialize());
    CHECK(gui_text_width("YAAI",GUI_DISPLAY) > gui_text_width("YAAI",GUI_BODY));
    CHECK(gui_text_width("",GUI_BODY) == 0);
    CHECK(gui_blend(0x123456,0xabcdef,0) == 0x123456);
    CHECK(gui_blend(0x123456,0xabcdef,255) == 0xabcdef);
    CHECK(gui_blend(0,0xffffff,128) == 0x808080);
    uint32_t guarded[16 * 12 + 2] = {0};
    guarded[0] = guarded[16 * 12 + 1] = 0xdecafbad;
    BootInfo screen = {0};
    screen.width = screen.pixels_per_scanline = 16;
    screen.height = 12;
    screen.pixel_format = 1;
    screen.framebuffer = guarded + 1;
    gui_round_rect(&screen,(GuiRect){-5,-5,30,30},12,0xffffff,255);
    gui_shadow(&screen,(GuiRect){2,2,8,6},3);
    gui_line(&screen,-8,-8,24,20,2,0xff00ff);
    gui_text(&screen,-8,-8,"YAAI",GUI_HEADING,0xffffff);
    gui_mono_character(&screen,'W',14,10,0xffffff,0);
    CHECK(guarded[0] == 0xdecafbad && guarded[16 * 12 + 1] == 0xdecafbad);
    CHECK(gui_wallpaper(&screen));
    CHECK(guarded[0] == 0xdecafbad && guarded[16 * 12 + 1] == 0xdecafbad);
    return 0;
}
