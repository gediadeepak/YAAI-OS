#include "editor.h"
static char clipboard[FILE_BYTES];
static unsigned clipboard_size;
static unsigned minimum(unsigned a, unsigned b) { return a < b ? a : b; }
static void snapshot(Editor *e) {
    EditSnapshot *s = &e->history[e->current];
    for (unsigned i = 0; i <= e->length; ++i)
        s->text[i] = e->text[i];
    s->length = e->length;
    s->cursor = e->cursor;
    s->anchor = e->anchor;
}
static void record(Editor *e) {
    if (e->current + 1 == EDIT_HISTORY) {
        for (unsigned i = 1; i < EDIT_HISTORY; ++i)
            e->history[i - 1] = e->history[i];
        --e->current;
    }
    ++e->current;
    e->count = e->current + 1;
    snapshot(e);
}
static void restore(Editor *e) {
    EditSnapshot *s = &e->history[e->current];
    for (unsigned i = 0; i <= s->length; ++i)
        e->text[i] = s->text[i];
    e->length = s->length;
    e->cursor = s->cursor;
    e->anchor = s->anchor;
}
void editor_reset(Editor *e, const char *text, unsigned length) {
    e->length = minimum(length, FILE_BYTES);
    for (unsigned i = 0; i < e->length; ++i)
        e->text[i] = text[i];
    e->text[e->length] = 0;
    e->cursor = e->anchor = e->length;
    e->scroll = 0;
    e->columns = 60;
    e->rows = 10;
    e->current = 0;
    e->count = 1;
    snapshot(e);
}
static unsigned line_start(const Editor *e, unsigned row) {
    unsigned line = 0, column = 0;
    if (!row)
        return 0;
    for (unsigned i = 0; i < e->length; ++i) {
        if (e->text[i] == '\n' || ++column == e->columns) {
            column = 0;
            if (++line == row)
                return i + 1;
        }
    }
    return e->length;
}
unsigned editor_line(const Editor *e, unsigned position) {
    unsigned line = 0, column = 0;
    for (unsigned i = 0; i < position && i < e->length; ++i)
        if (e->text[i] == '\n' || ++column == e->columns) {
            ++line;
            column = 0;
        }
    return line;
}
unsigned editor_column(const Editor *e, unsigned position) {
    unsigned column = 0;
    for (unsigned i = 0; i < position && i < e->length; ++i)
        if (e->text[i] == '\n' || ++column == e->columns)
            column = 0;
    return column;
}
static unsigned at(const Editor *e, unsigned row, unsigned column) {
    unsigned last_row = editor_line(e, e->length);
    if (row > last_row)
        row = last_row;
    unsigned start = line_start(e, row), end = start;
    while (end < e->length && e->text[end] != '\n' && end - start < e->columns)
        ++end;
    // A wrapped line's final boundary belongs to the next visual line.
    if (end - start == e->columns && end > start)
        --end;
    return start + minimum(column, end - start);
}
static void reveal(Editor *e) {
    unsigned row = editor_line(e, e->cursor);
    if (row < e->scroll)
        e->scroll = row;
    if (row >= e->scroll + e->rows)
        e->scroll = row - e->rows + 1;
}
void editor_layout(Editor *e, unsigned columns, unsigned rows) {
    unsigned c = columns ? columns : 1, r = rows ? rows : 1;
    if (e->columns != c || e->rows != r) {
        e->columns = c;
        e->rows = r;
        reveal(e);
    }
}
void editor_point(Editor *e, unsigned row, unsigned column, int extend) {
    e->cursor = row > editor_line(e, e->length) ? e->length : at(e, row, column);
    if (!extend)
        e->anchor = e->cursor;
}
void editor_select(Editor *e, unsigned first, unsigned end) {
    e->anchor = minimum(first, e->length);
    e->cursor = minimum(end, e->length);
    reveal(e);
}
int editor_replace_selection(Editor *e, const char *bytes, unsigned size) {
    unsigned first = minimum(e->cursor, e->anchor), last = e->cursor > e->anchor ? e->cursor : e->anchor;
    if (e->length - (last - first) + size > FILE_BYTES)
        return -1;
    if (first == last && !size)
        return 0;
    snapshot(e); // Preserve the selection/caret immediately before an edit.
    unsigned tail = e->length - last;
    if (size > last - first)
        for (unsigned i = tail + 1; i; --i)
            e->text[first + size + i - 1] = e->text[last + i - 1];
    else
        for (unsigned i = 0; i <= tail; ++i)
            e->text[first + size + i] = e->text[last + i];
    for (unsigned i = 0; i < size; ++i)
        e->text[first + i] = bytes[i];
    e->length = e->length - (last - first) + size;
    e->cursor = e->anchor = first + size;
    record(e);
    reveal(e);
    return 1;
}
int editor_key(Editor *e, unsigned key) {
    if (key == EDIT_UNDO || key == EDIT_REDO) {
        if (key == EDIT_UNDO) {
            if (!e->current)
                return 0;
            --e->current;
        } else {
            if (e->current + 1 >= e->count)
                return 0;
            ++e->current;
        }
        restore(e);
        reveal(e);
        return 1;
    }
    if (key == EDIT_SELECT_ALL) {
        e->anchor = 0;
        e->cursor = e->length;
        reveal(e);
        return 0;
    }
    if (key == EDIT_COPY || key == EDIT_CUT) {
        unsigned first = minimum(e->cursor, e->anchor), last = e->cursor > e->anchor ? e->cursor : e->anchor;
        if (first == last)
            return 0;
        clipboard_size = last - first;
        for (unsigned i = 0; i < clipboard_size; ++i)
            clipboard[i] = e->text[first + i];
        return key == EDIT_CUT ? editor_replace_selection(e, 0, 0) : 0;
    }
    if (key == EDIT_PASTE)
        return clipboard_size ? editor_replace_selection(e, clipboard, clipboard_size) : 0;
    int extend = key >= EDIT_LEFT + EDIT_SHIFT && key <= EDIT_DELETE + EDIT_SHIFT;
    unsigned command = extend ? key - EDIT_SHIFT : key;
    if (command >= EDIT_LEFT && command <= EDIT_PAGE_DOWN) {
        unsigned row = editor_line(e, e->cursor), column = editor_column(e, e->cursor);
        if (command == EDIT_LEFT) {
            if (!extend && e->cursor != e->anchor)
                e->cursor = minimum(e->cursor, e->anchor);
            else if (e->cursor)
                --e->cursor;
        } else if (command == EDIT_RIGHT) {
            if (!extend && e->cursor != e->anchor)
                e->cursor = e->cursor > e->anchor ? e->cursor : e->anchor;
            else if (e->cursor < e->length)
                ++e->cursor;
        } else if (command == EDIT_HOME)
            e->cursor = line_start(e, row);
        else if (command == EDIT_END) {
            e->cursor = line_start(e, row);
            unsigned used = 0;
            while (e->cursor < e->length && e->text[e->cursor] != '\n' && used++ < e->columns)
                ++e->cursor;
        } else if (command == EDIT_UP)
            e->cursor = at(e, row ? row - 1 : 0, column);
        else if (command == EDIT_DOWN)
            e->cursor = at(e, row + 1, column);
        else if (command == EDIT_PAGE_UP)
            e->cursor = at(e, row > e->rows ? row - e->rows : 0, column);
        else
            e->cursor = at(e, row + e->rows, column);
        if (!extend)
            e->anchor = e->cursor;
        reveal(e);
        return 0;
    }
    if (key == '\b' || key == EDIT_DELETE) {
        unsigned old_cursor = e->cursor, old_anchor = e->anchor;
        if (e->cursor == e->anchor) {
            if (key == '\b') {
                if (!e->cursor)
                    return 0;
                --e->anchor;
            } else {
                if (e->cursor == e->length)
                    return 0;
                ++e->anchor;
            }
        }
        int result = editor_replace_selection(e, 0, 0);
        if (result > 0) {
            e->history[e->current - 1].cursor = old_cursor;
            e->history[e->current - 1].anchor = old_anchor;
        }
        return result;
    }
    if (key == '\n' || (key >= 32 && key <= 126)) {
        char c = (char)key;
        return editor_replace_selection(e, &c, 1);
    }
    return 0;
}
