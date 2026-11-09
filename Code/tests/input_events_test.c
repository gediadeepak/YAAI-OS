#include "../input_events.h"
#include <limits.h>
#include "../edit_keys.h"
#include "../desktop_keys.h"
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
static void key(unsigned code, int value) { input_events_feed(1,(uint16_t)code,value); }
static void sync(void) { input_events_feed(0,0,0); }
int input_events_test(void)
{
    PlatformPointer event;
    input_events_initialize(800,600);
    CHECK(!input_events_key_pending() && !input_events_pointer_read(&event));
    key(30,1); key(30,0); CHECK(input_events_key_read() == 'a' && !input_events_key_read());
    key(42,1); key(54,1); key(42,0); key(30,2); CHECK(input_events_key_read() == 'A');
    key(54,0); key(58,1); key(58,2); key(58,0); key(30,1); CHECK(input_events_key_read() == 'A');
    key(42,1); key(30,1); key(2,1); CHECK(input_events_key_read() == 'a' && input_events_key_read() == '!');
    key(42,0); key(59,1); key(60,1); key(61,1);
    CHECK(input_events_key_read() == 1 && input_events_key_read() == 2 && input_events_key_read() == 3);
    key(68,1); CHECK(input_events_key_read() == 17);
    key(88,1); CHECK(input_events_key_read() == 30);
    key(62,1); key(64,1); key(65,1); key(15,1);
    CHECK(input_events_key_read() == 4 && input_events_key_read() == 6 && input_events_key_read() == 7 && input_events_key_read() == '\t');
    key(28,1); key(14,1); key(1,1); key(96,1);
    CHECK(input_events_key_read() == '\n' && input_events_key_read() == '\b' && input_events_key_read() == 27 && input_events_key_read() == '\n');
    key(65535,1); key(30,3); key(30,-1); CHECK(!input_events_key_read());
    key(105,1);CHECK((unsigned char)input_events_key_read()==EDIT_LEFT);
    key(42,1);key(106,1);key(42,0);CHECK((unsigned char)input_events_key_read()==EDIT_RIGHT+EDIT_SHIFT);
    key(29,1);key(30,1);key(46,1);key(47,1);key(44,1);key(21,1);key(29,0);
    CHECK((unsigned char)input_events_key_read()==EDIT_SELECT_ALL);
    CHECK((unsigned char)input_events_key_read()==EDIT_COPY);
    CHECK((unsigned char)input_events_key_read()==EDIT_PASTE);
    CHECK((unsigned char)input_events_key_read()==EDIT_UNDO);
    CHECK((unsigned char)input_events_key_read()==EDIT_REDO);
    key(56,1);key(15,1);key(15,2);key(42,1);key(15,1);key(42,0);
    key(105,1);key(106,1);key(108,1);key(68,1);key(67,1);key(56,0);
    CHECK(input_events_key_read()==DESKTOP_NEXT && input_events_key_read()==DESKTOP_NEXT);
    CHECK(input_events_key_read()==DESKTOP_PREVIOUS);
    CHECK(input_events_key_read()==DESKTOP_LEFT && input_events_key_read()==DESKTOP_RIGHT);
    CHECK(input_events_key_read()==DESKTOP_RESTORE && input_events_key_read()==DESKTOP_MAXIMIZE);
    CHECK(input_events_key_read()==DESKTOP_MINIMIZE && input_events_key_read()==DESKTOP_SWITCH_END);
    key(100,1);key(56,1);key(100,0);CHECK(!input_events_key_read());
    key(56,0);CHECK(input_events_key_read()==DESKTOP_SWITCH_END);
    key(15,1);CHECK(input_events_key_read()=='\t');

    input_events_feed(3,0,32767); input_events_feed(3,1,0);
    CHECK(!input_events_pointer_read(&event)); sync();
    CHECK(input_events_pointer_read(&event) && event.x == 799 && event.y == 0 && !event.buttons);
    key(272,1); sync(); key(272,0); sync();
    CHECK(input_events_pointer_read(&event) && event.buttons == 1);
    CHECK(input_events_pointer_read(&event) && !event.buttons); // Do not merge away fast clicks.
    key(273,1); key(274,1); sync(); CHECK(input_events_pointer_read(&event) && event.buttons == 6);
    key(273,0); key(274,0); key(330,1); sync(); CHECK(input_events_pointer_read(&event) && event.buttons == 1);
    input_events_feed(0,3,0); key(272,1); input_events_feed(3,0,0); sync();
    CHECK(input_events_pointer_read(&event) && !event.buttons && event.x == 799);
    input_events_feed(2,0,INT_MIN); input_events_feed(2,1,INT_MAX); sync();
    CHECK(input_events_pointer_read(&event) && event.x == 0 && event.y == 599);
    input_events_absolute_range(0,-100,100); input_events_feed(3,0,0); sync();
    CHECK(input_events_pointer_read(&event) && event.x == 399);
    input_events_absolute_range(0,INT_MIN,INT_MAX); input_events_feed(3,0,INT_MAX); sync();
    CHECK(input_events_pointer_read(&event) && event.x == 799);
    input_events_absolute_range(0,5,5); input_events_absolute_range(3,0,2);
    for (unsigned i = 0; i < 400; ++i) { key(272,(int)(i & 1)); sync(); key(30,1); }
    unsigned count = 0;
    while (input_events_key_read()) CHECK(++count < 128);
    count = 0;
    while (input_events_pointer_read(&event)) CHECK(++count < 128);
    CHECK(event.buttons == 1); key(272,0); sync();
    CHECK(input_events_pointer_read(&event) && !event.buttons);
    input_events_initialize(0,0); input_events_feed(3,0,INT_MAX); input_events_feed(3,1,INT_MIN); sync();
    // A degenerate one-pixel surface remains bounded, whether a motion event is emitted or not.
    if (input_events_pointer_read(&event)) CHECK(!event.x && !event.y);
    return 0;
}
