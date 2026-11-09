#include "platform/platform.h"
#include "boot_info.h"
#include "screen.h"
#include "console.h"

#include "shell.h"
#include "memory.h"
#include "desktop.h"
#include "gui.h"
#include "brand.h"
#include "settings.h"
#include "files.h"

uint32_t color(const BootInfo *boot, uint8_t red, uint8_t green, uint8_t blue)
{
    if (boot->pixel_format == 0) {
        return red | ((uint32_t)green << 8) | ((uint32_t)blue << 16);
    }
    return blue | ((uint32_t)green << 8) | ((uint32_t)red << 16);
}

// A small built-in 5x7 font keeps rendering independent of firmware services.
static const uint8_t font[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
    {31,4,4,4,4,4,31}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};

void pixel(const BootInfo *boot, int x, int y, uint32_t value)
{
    if (x >= 0 && y >= 0 && (uint32_t)x < boot->width && (uint32_t)y < boot->height) {
        boot->framebuffer[(uint64_t)y * boot->pixels_per_scanline + x] = value;
    }
}

static void draw_character(const BootInfo *boot, char character, int left, int top,
                           int scale, uint32_t foreground, uint32_t background, int opaque)
{
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
    };
    static const uint8_t lowercase[26][7] = {
        {0,0,14,1,15,17,15}, {16,16,30,17,17,17,30},
        {0,0,14,16,16,17,14}, {1,1,15,17,17,17,15},
        {0,0,14,17,31,16,14}, {6,9,8,28,8,8,8},
        {0,15,17,17,15,1,14}, {16,16,30,17,17,17,17},
        {4,0,12,4,4,4,14}, {2,0,6,2,2,18,12},
        {16,16,18,20,24,20,18}, {12,4,4,4,4,4,14},
        {0,0,26,21,21,21,21}, {0,0,30,17,17,17,17},
        {0,0,14,17,17,17,14}, {0,30,17,17,30,16,16},
        {0,15,17,17,15,1,1}, {0,0,22,25,16,16,16},
        {0,0,15,16,14,1,30}, {8,8,28,8,8,9,6},
        {0,0,17,17,17,19,13}, {0,0,17,17,17,10,4},
        {0,0,17,17,21,21,10}, {0,0,17,10,4,10,17},
        {0,17,17,17,15,1,14}, {0,0,31,2,4,8,31}
    };
    static const char punctuation[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    static const uint8_t symbols[][7] = {
        {4,4,4,4,4,0,4}, {10,10,10,0,0,0,0},
        {10,10,31,10,31,10,10}, {4,15,20,14,5,30,4},
        {24,25,2,4,8,19,3}, {12,18,20,8,21,18,13},
        {4,4,8,0,0,0,0}, {2,4,8,8,8,4,2},
        {8,4,2,2,2,4,8}, {0,21,14,31,14,21,0},
        {0,4,4,31,4,4,0}, {0,0,0,0,4,4,8},
        {0,0,0,31,0,0,0}, {0,0,0,0,0,12,12},
        {1,2,2,4,8,8,16}, {0,12,12,0,12,12,0},
        {0,12,12,0,4,4,8}, {1,2,4,8,4,2,1},
        {0,0,31,0,31,0,0}, {16,8,4,2,4,8,16},
        {14,17,1,2,4,0,4}, {14,17,23,21,23,16,14},
        {14,8,8,8,8,8,14}, {16,8,8,4,2,2,1},
        {14,2,2,2,2,2,14}, {4,10,17,0,0,0,0},
        {0,0,0,0,0,0,31}, {8,4,2,0,0,0,0},
        {2,4,4,8,4,4,2}, {4,4,4,4,4,4,4},
        {8,4,4,2,4,4,8}, {0,0,9,22,0,0,0}
    };
    const uint8_t *glyph = 0;
    if (character >= 'a' && character <= 'z') glyph = lowercase[character - 'a'];
    else if (character >= 'A' && character <= 'Z') glyph = font[character - 'A'];
    else if (character >= '0' && character <= '9') glyph = digits[character - '0'];
    else {
        for (unsigned index = 0; punctuation[index]; ++index) {
            if (punctuation[index] == character) glyph = symbols[index];
        }
    }
    for (int y = 0; y < 9 * scale; ++y) {
        for (int x = 0; x < 6 * scale; ++x) {
            int lit = glyph && y < 7 * scale && x < 5 * scale &&
                      (glyph[y / scale] & (1u << (4 - x / scale)));
            if (lit || opaque) pixel(boot, left + x, top + y, lit ? foreground : background);
        }
    }
}

void screen_character(const BootInfo *boot, char character, int left, int top,
                      int scale, uint32_t foreground, uint32_t background)
{
    draw_character(boot, character, left, top, scale, foreground, background, 1);
}

void screen_character_transparent(const BootInfo *boot, char character, int left,
                                  int top, int scale, uint32_t foreground)
{
    draw_character(boot, character, left, top, scale, foreground, 0, 0);
}

static void text(const BootInfo *boot, const char *message, int top, int scale, uint32_t value)
{
    int length = 0;
    while (message[length]) {
        ++length;
    }
    int left = ((int)boot->width - (length * 6 - 1) * scale) / 2;
    for (int character = 0; character < length; ++character) {
        if (message[character] == '.') {
            for (int y = 5 * scale; y < 7 * scale; ++y) {
                for (int x = scale; x < 3 * scale; ++x) {
                    pixel(boot, left + character * 6 * scale + x, top + y, value);
                }
            }
            continue;
        }
        if (message[character] < 'A' || message[character] > 'Z') {
            continue;
        }
        const uint8_t *glyph = font[message[character] - 'A'];
        for (int y = 0; y < 7 * scale; ++y) {
            for (int x = 0; x < 5 * scale; ++x) {
                if (glyph[y / scale] & (1u << (4 - x / scale))) {
                    pixel(boot, left + character * 6 * scale + x, top + y, value);
                }
            }
        }
    }
}

static void draw_splash(const BootInfo *boot)
{
    int center_x = (int)boot->width / 2;
    int center_y = (int)boot->height / 2 - 62;
    for (uint32_t y = 0; y < boot->height; ++y) {
        for (uint32_t x = 0; x < boot->width; ++x) {
            int64_t dx = (int64_t)x - center_x;
            int64_t dy = (int64_t)y - center_y;
            int64_t distance = dx * dx + dy * dy;
            uint32_t glow = (uint32_t)(1800000 / (distance + 20000));
            uint32_t red = 5 + glow / 10;
            uint32_t green = 9 + glow / 4;
            uint32_t blue = 20 + glow / 2;
            // Cyan orbit with a subtle outer halo and a small opening on the right.
            if (distance > 86 * 86 && distance < 104 * 104) {
                green += 16;
                blue += 22;
            }
            if (distance > 92 * 92 && distance < 98 * 98 && !(dx > 0 && dy > -20 && dy < 20)) {
                red = 85; green = 220; blue = 235;
            }
            // A faceted light core gives the emblem a simple, original identity.
            int64_t abs_dx = dx < 0 ? -dx : dx;
            int64_t abs_dy = dy < 0 ? -dy : dy;
            if (abs_dx + abs_dy < 38) {
                red = dx < 0 ? 160 : 225;
                green = dx < 0 ? 225 : 247;
                blue = 255;
            }
            pixel(boot, (int)x, (int)y, color(boot, (uint8_t)red, (uint8_t)green, (uint8_t)blue));
        }
    }
    text(boot, OS_NAME, center_y + 145, 3, color(boot, 225, 238, 250));
    if (gui_initialize()) {
        int width = gui_text_width(OS_TAGLINE,GUI_SMALL);
        gui_text(boot,((int)boot->width - width) / 2,center_y + 190,OS_TAGLINE,GUI_SMALL,color(boot,153,180,199));
    }
}

_Noreturn void kernel_main(const BootInfo *boot)
{
    serial_write("YAAI OS: kernel entered after ExitBootServices\r\n");
    serial_write("PLATFORM: "); serial_write(platform_name()); serial_write("\r\n");
    console_initialize(boot);
    console_write("DISPLAY: "); console_decimal(boot->width); console_write(" x "); console_decimal(boot->height); console_write("\n");
    platform_exceptions_initialize();
    if (!memory_initialize(boot)) {
        console_write("MEMORY INITIALIZATION FAILED. RESTART QEMU.\n");
        platform_halt();
    }
    serial_write("MEMORY: physical page allocator ready\r\n");
    platform_storage_initialize(boot);
    settings_initialize(boot);
    files_initialize();
    platform_audio_volume(settings_current()->volume);
    draw_splash(boot);
    int playing = platform_audio_start(boot);
    serial_write(playing ? "AUDIO: startup chime playing\r\n" : "AUDIO: unavailable; continuing silently\r\n");
    int bar_top = (int)boot->height / 2 + 166;
    int bar_left = (int)boot->width / 2 - 120;
    // A timed splash animation, ready for real initialization steps as the OS grows.
    for (int frame = 0; frame <= 128; ++frame) {
        for (int x = 0; x < 240; ++x) {
            uint32_t fill = x * 128 <= frame * 240 ? color(boot, 90, 212, 225) : color(boot, 23, 42, 58);
            for (int y = 0; y < 2; ++y) {
                pixel(boot, bar_left + x, bar_top + y, fill);
            }
        }
        screen_wait_frame();
    }
    text(boot, "KERNEL READY", bar_top + 34, 1, color(boot, 125, 158, 176));
    serial_write("KERNEL: splash complete; memory map received\r\n");
    if (boot->desktop_buffer && boot->wallpaper_buffer && boot->width >= 640 && boot->height >= 480) {
        desktop_run(boot);
    }
    shell_run(boot);
}
