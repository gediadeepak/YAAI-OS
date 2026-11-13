#include "remote_desktop.h"
#include "platform/platform.h"

enum { WIDTH = 320, MAX_HEIGHT = 240, HEADER = 8, CHUNK = 32768 };
static uint8_t frame[HEADER + WIDTH * MAX_HEIGHT * 2];
static unsigned sent, size;
static int available, enabled, checked;

int remote_desktop_available(void) {
    if (!checked) {
        available = platform_remote_serial_initialize();
        checked = 1;
    }
    return available;
}

int remote_desktop_enabled(void) { return enabled; }

void remote_desktop_toggle(void) {
    if (remote_desktop_available()) {
        enabled = !enabled;
        sent = size = 0;
        for (unsigned i = 0; i < 32 && platform_remote_serial_read() >= 0; ++i) {}
    }
}

static void capture(const BootInfo *display) {
    unsigned height = WIDTH * display->height / display->width;
    if (height > MAX_HEIGHT) height = MAX_HEIGHT;
    frame[0] = 'Y'; frame[1] = 'A'; frame[2] = 'A'; frame[3] = 'I';
    frame[4] = WIDTH & 255; frame[5] = WIDTH >> 8;
    frame[6] = (uint8_t)height; frame[7] = (uint8_t)(height >> 8);
    size = HEADER;
    for (unsigned y = 0; y < height; ++y) {
        const volatile uint32_t *row = display->framebuffer +
            (uint64_t)(y * display->height / height) * display->pixels_per_scanline;
        for (unsigned x = 0; x < WIDTH; ++x) {
            uint32_t pixel = row[x * display->width / WIDTH];
            unsigned red = (pixel >> (display->pixel_format ? 16 : 0)) & 255;
            unsigned blue = (pixel >> (display->pixel_format ? 0 : 16)) & 255;
            unsigned green = (pixel >> 8) & 255;
            uint16_t packed = (uint16_t)((red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3));
            frame[size++] = (uint8_t)packed;
            frame[size++] = (uint8_t)(packed >> 8);
        }
    }
    sent = 0;
}

void remote_desktop_poll(const BootInfo *display) {
    if (!enabled) return;
    if (sent == size) {
        if (platform_remote_serial_read() != 'F') return;
        capture(display);
    }
    unsigned remaining = size - sent;
    sent += platform_remote_serial_write(frame + sent, remaining < CHUNK ? remaining : CHUNK);
}
