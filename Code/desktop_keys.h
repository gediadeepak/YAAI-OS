#ifndef YAAI_DESKTOP_KEYS_H
#define YAAI_DESKTOP_KEYS_H
enum {
    DESKTOP_NEXT = 21,
    DESKTOP_PREVIOUS,
    DESKTOP_MAXIMIZE,
    DESKTOP_LEFT,
    DESKTOP_RIGHT,
    DESKTOP_SWITCH_END,
    DESKTOP_RESTORE = 28,
    DESKTOP_MINIMIZE,
    DESKTOP_QUICK = 31
};
static inline char desktop_shortcut(unsigned code, unsigned shift) {
    return code == 16    ? DESKTOP_QUICK
           : code == 15  ? (shift ? DESKTOP_PREVIOUS : DESKTOP_NEXT)
           : code == 68  ? DESKTOP_MAXIMIZE
           : code == 105 ? DESKTOP_LEFT
           : code == 106 ? DESKTOP_RIGHT
           : code == 108 ? DESKTOP_RESTORE
           : code == 67  ? DESKTOP_MINIMIZE
                         : 0;
}
#endif
