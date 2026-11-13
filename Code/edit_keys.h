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
    EDIT_SAVE,
    EDIT_FIND,
    EDIT_REPLACE,
    EDIT_PREVIOUS_MATCH,
    EDIT_REPLACE_ONE,
    EDIT_REPLACE_ALL
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
           : letter == 'f' ? EDIT_FIND
           : letter == 'h' ? EDIT_REPLACE
           : letter == 'r' ? EDIT_REPLACE_ONE
                           : 0;
}
#endif
