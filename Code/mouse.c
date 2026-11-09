#include "mouse.h"
#include "ps2.h"

static int screen_width, screen_height;
static MouseState state;
static uint8_t packet[3], packet_size;

void mouse_reset(int width, int height)
{
    screen_width = width > 0 ? width : 1;
    screen_height = height > 0 ? height : 1;
    state.x = screen_width / 2;
    state.y = screen_height / 2;
    state.buttons = packet_size = 0;
}

int mouse_feed_byte(uint8_t byte, MouseState *event)
{
    if (!packet_size && !(byte & 8)) return 0;
    packet[packet_size++] = byte;
    if (packet_size != 3) return 0;
    packet_size = 0;
    // The sign is the ninth bit in the flags, not the eighth bit of the delta.
    if (!(packet[0] & 0xc0)) {
        state.x += packet[1] - ((packet[0] & 0x10) ? 256 : 0);
        state.y -= packet[2] - ((packet[0] & 0x20) ? 256 : 0);
        if (state.x < 0) state.x = 0;
        if (state.y < 0) state.y = 0;
        if (state.x >= screen_width) state.x = screen_width - 1;
        if (state.y >= screen_height) state.y = screen_height - 1;
    }
    state.buttons = packet[0] & 7;
    *event = state;
    return 1;
}

int mouse_read(MouseState *event)
{
    int byte;
    while ((byte = ps2_mouse_byte()) >= 0) {
        if (mouse_feed_byte((uint8_t)byte, event)) return 1;
    }
    return 0;
}
