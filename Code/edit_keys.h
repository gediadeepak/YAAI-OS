#ifndef YAAI_EDIT_KEYS_H
#define YAAI_EDIT_KEYS_H
enum {
    EDIT_LEFT = 128,
    EDIT_RIGHT,
    EDIT_UP,
    EDIT_DOWN,
    EDIT_HOME,
    EDIT_END,
    EDIT_PAGE_UP,
    EDIT_PAGE_DOWN,
    EDIT_DELETE,
    EDIT_SHIFT = 16,
    EDIT_SELECT_ALL = 176,
    EDIT_COPY,
    EDIT_CUT,
    EDIT_PASTE,
    EDIT_UNDO,
    EDIT_REDO,
    EDIT_SAVE
};
static inline unsigned edit_shortcut(char letter) {
    if (letter >= 'A' && letter <= 'Z')
        letter += 32;
    return letter == 'a'   ? EDIT_SELECT_ALL
           : letter == 'c' ? EDIT_COPY
           : letter == 'x' ? EDIT_CUT
           : letter == 'v' ? EDIT_PASTE
           : letter == 'z' ? EDIT_UNDO
           : letter == 'y' ? EDIT_REDO
           : letter == 's' ? EDIT_SAVE
                           : 0;
}
#endif
