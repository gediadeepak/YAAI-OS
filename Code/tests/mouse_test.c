#include "../mouse.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int mouse_test(void)
{
    MouseState state;
    mouse_reset(1000,600);
    CHECK(!mouse_feed_byte(0,&state));
    CHECK(!mouse_feed_byte(8,&state));
    CHECK(!mouse_feed_byte(200,&state));
    CHECK(mouse_feed_byte(200,&state));
    CHECK(state.x == 700 && state.y == 100 && state.buttons == 0);
    CHECK(!mouse_feed_byte(0x38,&state));
    CHECK(!mouse_feed_byte(56,&state));
    CHECK(mouse_feed_byte(56,&state));
    CHECK(state.x == 500 && state.y == 300);
    CHECK(!mouse_feed_byte(0xc9,&state));
    CHECK(!mouse_feed_byte(255,&state));
    CHECK(mouse_feed_byte(255,&state));
    CHECK(state.x == 500 && state.y == 300 && state.buttons == 1);
    mouse_reset(50,40);
    CHECK(!mouse_feed_byte(0x38,&state));
    CHECK(!mouse_feed_byte(1,&state));
    CHECK(mouse_feed_byte(1,&state));
    CHECK(state.x == 0 && state.y == 39);
    CHECK(!mouse_feed_byte(8,&state));
    CHECK(!mouse_feed_byte(255,&state));
    CHECK(mouse_feed_byte(255,&state));
    CHECK(state.x == 49 && state.y == 0 && state.buttons == 0);
    return 0;
}
