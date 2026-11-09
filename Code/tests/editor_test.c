#include "../editor.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static Editor editor;
static char full[FILE_BYTES];
static int equal(const char *s) {
    unsigned i = 0;
    for (; s[i]; ++i)
        if (editor.text[i] != s[i])
            return 0;
    return editor.length == i && editor.text[i] == 0;
}
int editor_test(void) {
    Editor *e = &editor;
    editor_reset(e, "hello world", 11);
    editor_key(e, EDIT_HOME);
    CHECK(!e->cursor);
    editor_key(e, 'X');
    CHECK(equal("Xhello world") && e->cursor == 1);
    editor_key(e, EDIT_UNDO);
    CHECK(equal("hello world") && !e->cursor);
    editor_key(e, EDIT_REDO);
    CHECK(equal("Xhello world") && e->cursor == 1);
    editor_reset(e, "abc", 3);
    editor_key(e, EDIT_LEFT);
    editor_key(e, EDIT_DELETE);
    CHECK(equal("ab"));
    editor_key(e, '\b');
    CHECK(equal("a"));
    editor_key(e, EDIT_UNDO);
    CHECK(equal("ab") && e->cursor == 2 && e->anchor == 2);
    editor_key(e, EDIT_UNDO);
    CHECK(equal("abc") && e->cursor == 2 && e->anchor == 2);
    editor_key(e, EDIT_SELECT_ALL);
    editor_key(e, EDIT_COPY);
    editor_reset(e, "x", 1);
    editor_key(e, EDIT_PASTE);
    CHECK(equal("xabc"));
    editor_key(e, EDIT_UNDO);
    CHECK(equal("x"));
    editor_key(e, EDIT_REDO);
    CHECK(equal("xabc"));
    editor_key(e, EDIT_SELECT_ALL);
    editor_key(e, EDIT_CUT);
    CHECK(equal(""));
    editor_key(e, EDIT_UNDO);
    CHECK(equal("xabc"));
    editor_key(e, EDIT_UNDO);
    CHECK(equal("x"));
    editor_key(e, 'Z');
    editor_key(e, EDIT_REDO);
    CHECK(equal("xZ"));
    editor_reset(e, "one\ntwo\nthree", 13);
    editor_layout(e, 20, 2);
    editor_key(e, EDIT_HOME);
    CHECK(e->cursor == 8);
    editor_key(e, EDIT_UP);
    CHECK(e->cursor == 4);
    editor_key(e, EDIT_RIGHT + EDIT_SHIFT);
    CHECK(e->anchor == 4 && e->cursor == 5);
    editor_key(e, 'T');
    CHECK(equal("one\nTwo\nthree"));
    editor_key(e, EDIT_UNDO);
    CHECK(equal("one\ntwo\nthree"));
    editor_key(e, EDIT_PAGE_DOWN);
    CHECK(e->cursor == 9 && e->scroll == 1);
    editor_point(e, 0, 2, 0);
    CHECK(e->cursor == 2 && e->anchor == 2);
    editor_point(e, 1, 2, 1);
    CHECK(e->cursor == 6 && e->anchor == 2);
    editor_key(e, EDIT_COPY);
    editor_key(e, EDIT_DELETE);
    CHECK(equal("ono\nthree"));
    editor_key(e, EDIT_PASTE);
    CHECK(equal("one\ntwo\nthree"));
    editor_reset(e, "abcdef", 6);
    editor_layout(e, 3, 1);
    CHECK(editor_line(e, 6) == 2 && e->scroll == 2);
    editor_key(e, EDIT_UP);
    CHECK(e->cursor == 3 && e->scroll == 1);
    editor_key(e, EDIT_END);
    CHECK(e->cursor == 6);
    editor_point(e, 0, 99, 0);
    CHECK(e->cursor == 2); // Clicks never leak onto the next wrapped row.
    editor_reset(e, "", 0);
    for (unsigned i = 0; i < 40; ++i)
        editor_key(e, 'a');
    for (unsigned i = 0; i < 50; ++i)
        editor_key(e, EDIT_UNDO);
    CHECK(e->length == 24);
    for (unsigned i = 0; i < 50; ++i)
        editor_key(e, EDIT_REDO);
    CHECK(e->length == 40);
    for (unsigned i = 0; i < FILE_BYTES; ++i)
        full[i] = 'b';
    editor_reset(e, full, FILE_BYTES);
    CHECK(editor_key(e, 'x') == -1 && e->length == FILE_BYTES && e->text[FILE_BYTES] == 0);
    editor_key(e, EDIT_SELECT_ALL);
    editor_key(e, EDIT_COPY);
    editor_reset(e, "x", 1);
    CHECK(editor_key(e, EDIT_PASTE) == -1 && equal("x")); // Oversized paste is atomic.
    editor_key(e, EDIT_SELECT_ALL);
    CHECK(editor_key(e, EDIT_PASTE) == 1 && e->length == FILE_BYTES);
    editor_key(e, EDIT_UNDO);
    CHECK(equal("x"));
    return 0;
}
