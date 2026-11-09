#include "input_events.h"
#include <limits.h>
#include "edit_keys.h"
#include "desktop_keys.h"
enum { CAPACITY = 128 };
static char keys[CAPACITY];
static PlatformPointer pointers[CAPACITY], pointer;
static unsigned key_head, key_tail, pointer_head, pointer_tail, shifts, caps, controls, alts;
static int width, height, changed, dropped;
static int32_t axis_min[2], axis_max[2];

void input_events_initialize(unsigned w, unsigned h)
{
    width = w && w <= INT_MAX ? (int)w : 1;
    height = h && h <= INT_MAX ? (int)h : 1;
    key_head = key_tail = pointer_head = pointer_tail = shifts = caps = controls = alts = 0;
    changed = dropped = 0;
    pointer = (PlatformPointer){width / 2,height / 2,0};
    axis_min[0] = axis_min[1] = 0; axis_max[0] = axis_max[1] = 32767;
}
void input_events_absolute_range(unsigned axis, int32_t minimum, int32_t maximum)
{
    if (axis < 2 && maximum > minimum) { axis_min[axis] = minimum; axis_max[axis] = maximum; }
}
static int clamp(int64_t value, int bound)
{
    return value < 0 ? 0 : value >= bound ? bound - 1 : (int)value;
}
static void character(unsigned code, int32_t value)
{
    static const char normal[58] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    static const char shifted[58] = {
        0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    };
    if (value < 0 || value > 2) return;
    if (code == 42 || code == 54) {
        unsigned mask = code == 42 ? 1 : 2;
        if (value) shifts |= mask; else shifts &= ~mask;
        return;
    }
    if (code == 58) { if (value == 1) caps ^= 1; return; }
    if(code==29||code==97){unsigned mask=code==29?1:2;if(value)controls|=mask;else controls&=~mask;return;}
    if(code==56||code==100) {
        unsigned mask=code==56?1:2;
        if(value) alts|=mask;else {
            alts&=~mask;
            unsigned next=(key_tail+1)%CAPACITY;
            if(!alts&&next!=key_head){keys[key_tail]=DESKTOP_SWITCH_END;key_tail=next;}
        }
        return;
    }
    if (!value) return;
    char key = code < 58 ? (shifts ? shifted[code] : normal[code]) :
        code >= 59 && code <= 65 ? (char)(code - 58) : code == 66 ? 18 : code == 67 ? 19 : code == 68 ? 17 : code == 87 ? 20 : code == 96 ? '\n' : 0;
    if (code < 58 && normal[code] >= 'a' && normal[code] <= 'z')
        key = (!!shifts ^ caps) ? (char)(normal[code] - 'a' + 'A') : normal[code];
    unsigned navigation=code==105?EDIT_LEFT:code==106?EDIT_RIGHT:code==103?EDIT_UP:code==108?EDIT_DOWN:
        code==102?EDIT_HOME:code==107?EDIT_END:code==104?EDIT_PAGE_UP:code==109?EDIT_PAGE_DOWN:code==111?EDIT_DELETE:0;
    if(navigation)key=(char)(navigation+(shifts&&navigation<=EDIT_PAGE_DOWN?EDIT_SHIFT:0));
    else if(controls&&code<58)key=(char)edit_shortcut(normal[code]);
    if(alts) {char action=desktop_shortcut(code,shifts);if(action)key=action;}
    unsigned next = (key_tail + 1) % CAPACITY;
    if (key && next != key_head) { keys[key_tail] = key; key_tail = next; }
}
void input_events_feed(uint16_t type, uint16_t code, int32_t value)
{
    if (type == 0 && code == 3) { // SYN_DROPPED: discard partial reports and release held controls.
        dropped = 1; shifts = controls = alts = 0; keys[key_tail]=DESKTOP_SWITCH_END; key_tail=(key_tail+1)%CAPACITY; if(key_tail==key_head)key_head=(key_head+1)%CAPACITY; pointer.buttons = 0; changed = 1; return;
    }
    if (type == 0 && code == 0) {
        dropped = 0;
        if (changed) {
            unsigned next = (pointer_tail + 1) % CAPACITY;
            // Preserve the latest button state even if a consumer falls behind.
            if (next == pointer_head) pointer_head = (pointer_head + 1) % CAPACITY;
            pointers[pointer_tail] = pointer; pointer_tail = next; changed = 0;
        }
        return;
    }
    if (dropped) return;
    if (type == 1 && ((code >= 272 && code <= 274) || code == 330)) {
        if (value != 0 && value != 1) return;
        unsigned mask = code == 330 ? 1 : 1u << (code - 272);
        if (value) pointer.buttons |= (uint8_t)mask; else pointer.buttons &= (uint8_t)~mask;
        changed = 1;
    } else if (type == 1) character(code,value);
    else if ((type == 2 || type == 3) && code < 2) {
        int old = code ? pointer.y : pointer.x;
        int bound = code ? height : width;
        int coordinate;
        if (type == 2) coordinate = clamp((int64_t)old + value,bound);
        else {
            int64_t clipped = value;
            if (clipped < axis_min[code]) clipped = axis_min[code];
            if (clipped > axis_max[code]) clipped = axis_max[code];
            coordinate = (int)((clipped - axis_min[code]) * (bound - 1) /
                ((int64_t)axis_max[code] - axis_min[code]));
        }
        if (code) pointer.y = coordinate; else pointer.x = coordinate;
        changed |= coordinate != old;
    }
}
int input_events_key_pending(void) { return key_head != key_tail; }
char input_events_key_read(void)
{
    if (key_head == key_tail) return 0;
    char key = keys[key_head]; key_head = (key_head + 1) % CAPACITY; return key;
}
int input_events_pointer_read(PlatformPointer *event)
{
    if (!event || pointer_head == pointer_tail) return 0;
    *event = pointers[pointer_head]; pointer_head = (pointer_head + 1) % CAPACITY; return 1;
}
