#include <stdint.h>
#include "keyboard.h"
#include "ps2.h"
#include "edit_keys.h"
#include "desktop_keys.h"

static uint8_t shifts, caps_lock, extended, pause_bytes, controls, alts;

int keyboard_initialize(void)
{
    shifts = caps_lock = extended = pause_bytes = controls = alts = 0;
    return ps2_initialize();
}
char keyboard_read(void)
{
    static const char normal[58] = {
        0,0,'1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';', '\'', '`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    static const char shifted[58] = {
        0,0,'!','@','#','$','%','^','&','*','(',')','_','+', '\b','\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    };
    int value = ps2_keyboard_byte();
    if (value < 0) return 0;
    uint8_t code = (uint8_t)value;
    if (pause_bytes) { --pause_bytes; return 0; }
    if (code == 0xe1) { pause_bytes = 5; return 0; }
    if (code == 0xe0) { extended = 1; return 0; }
    uint8_t is_extended = extended;
    extended = 0;
    if ((code & 0x7f) == 0x1d) {
        uint8_t mask = is_extended ? 2 : 1;
        if(code & 0x80) controls &= (uint8_t)~mask; else controls |= mask;
        return 0;
    }
    if ((code & 0x7f) == 0x38) {
        unsigned mask=is_extended?2:1;
        if(code&0x80) {alts &= (uint8_t)~mask;return alts?0:DESKTOP_SWITCH_END;}
        alts |= (uint8_t)mask;return 0;
    }
    if(alts && !(code&0x80)) {
        unsigned nav=is_extended?(code==0x4b?105:code==0x4d?106:code==0x50?108:0):code;
        char action=desktop_shortcut(nav,shifts);
        if(action)return action;
    }
    if (is_extended) {
        unsigned key = code==0x4b?EDIT_LEFT:code==0x4d?EDIT_RIGHT:code==0x48?EDIT_UP:code==0x50?EDIT_DOWN:
            code==0x47?EDIT_HOME:code==0x4f?EDIT_END:code==0x49?EDIT_PAGE_UP:code==0x51?EDIT_PAGE_DOWN:code==0x53?EDIT_DELETE:0;
        if(key)return (char)(key + (shifts && key<=EDIT_PAGE_DOWN ? EDIT_SHIFT : 0));
        return code == 0x1c ? '\n' : 0;
    }
    if (code == 0x2a) { shifts |= 1; return 0; }
    if (code == 0x36) { shifts |= 2; return 0; }
    if (code == 0xaa) { shifts &= ~1u; return 0; }
    if (code == 0xb6) { shifts &= ~2u; return 0; }
    if (code == 0x3a) { caps_lock ^= 1; return 0; }
    if (code == 1) return 27;
    if (code == 0x3b) return 1; // F1 opens Terminal.
    if (code == 0x3c) return 2; // F2 opens System.
    if (code == 0x3d) return 3; // F3 opens Devices and Sound.
    if (code == 0x3e) return 4; // F4 opens Files and Notes.
    if (code == 0x3f) return 5; // F5 opens About.
    if (code == 0x40) return 6; // F6 saves the current document.
    if (code == 0x41) return 7; // F7 confirms discarding unsaved notes.
    if (code == 0x42) return 18; // F8: Files actions.
    if (code == 0x43) return 19; // F9: New folder.
    if (code == 0x44) return 17; // F10 opens the desktop menu.
    if (code == 0x57) return 20; // F11 launches Calculator.
    if (code >= sizeof(normal)) return 0;
    char character = normal[code];
    if(controls)return (char)edit_shortcut(character);
    if (character >= 'a' && character <= 'z') {
        return (!!shifts ^ caps_lock) ? (char)(character - 'a' + 'A') : character;
    }
    return shifts ? shifted[code] : character;
}
