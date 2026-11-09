#include "file_app.h"
#include "apps.h"
#include "editor.h"
#include "screen.h"
static const AppFiles *fs;
static Editor document;
#define text document.text
#define length document.length
static char filename[FILE_NAME], saved_name[FILE_NAME], original[FILE_NAME];
static char incoming[FILE_BYTES + 1], saved_text[FILE_BYTES + 1];
static unsigned saved_length, open_id, editor_parent;
static int initialized, dirty, name_focus, name_selected, discard_pending, selection_drag;
static const char *message = "New note - type here or open a text file from Files";
void file_app_bind(const AppServices *services) { fs = services->files; }
static void copy(char *to, const char *from, unsigned size) {
    unsigned i = 0;
    for (; from[i] && i + 1 < size; ++i)
        to[i] = from[i];
    to[i] = 0;
}
static int equal(const char *a, const char *b) {
    unsigned i = 0;
    for (;; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}
static void mark_clean(void) {
    for (unsigned i = 0; i <= length; ++i)
        saved_text[i] = text[i];
    saved_length = length;
    copy(saved_name, filename, FILE_NAME);
    dirty = 0;
}
static void update_dirty(void) {
    dirty = length != saved_length || !equal(filename, saved_name);
    for (unsigned i = 0; !dirty && i < length; ++i)
        dirty = text[i] != saved_text[i];
}
void file_app_open(void) {
    if (initialized)
        return;
    initialized = 1;
    editor_reset(&document, "", 0);
    copy(filename, "untitled.txt", FILE_NAME);
    original[0] = 0;
    open_id = editor_parent = 0;
    name_focus = name_selected = selection_drag = discard_pending = 0;
    mark_clean();
}
unsigned file_app_document_id(void) { return open_id; }
int file_app_load(unsigned id) {
    file_app_open();
    if (dirty) {
        message = "Save or press F7 twice to discard before opening another file";
        return 0;
    }
    FileEntry e;
    FileResult r = fs->entry(id, &e);
    if (r != FILE_OK || e.flags & (FILE_DIRECTORY | FILE_TRASHED)) {
        message = r == FILE_OK ? "Restore this file from Trash before opening" : files_error(r);
        return 0;
    }
    unsigned parent = e.parent;
    for (unsigned depth = 0; parent && depth < FILE_COUNT; ++depth) {
        FileEntry p;
        if (fs->entry(parent, &p) != FILE_OK || p.flags & FILE_TRASHED) {
            message = "Restore the parent folder before opening";
            return 0;
        }
        parent = p.parent;
    }
    unsigned n = FILE_BYTES;
    r = fs->read_id(id, incoming, &n);
    if (r != FILE_OK) {
        message = files_error(r);
        return 0;
    }
    for (unsigned i = 0; i < n; ++i)
        if ((unsigned char)incoming[i] > 126 || ((unsigned char)incoming[i] < 32 && incoming[i] != '\n' &&
                                                 incoming[i] != '\r' && incoming[i] != '\t')) {
            message = "Notes supports plain ASCII text files";
            return 0;
        }
    editor_reset(&document, incoming, n);
    copy(filename, e.name, FILE_NAME);
    copy(original, e.name, FILE_NAME);
    open_id = id;
    editor_parent = e.parent;
    name_focus = name_selected = discard_pending = selection_drag = 0;
    mark_clean();
    message = "Opened in Notes";
    return 1;
}
int file_app_new(unsigned parent) {
    file_app_open();
    if (dirty) {
        message = "Save or discard the open note before creating another";
        return 0;
    }
    if (parent) {
        FileEntry e;
        if (fs->entry(parent, &e) != FILE_OK || !(e.flags & FILE_DIRECTORY) || e.flags & FILE_TRASHED)
            return 0;
    }
    FileEntry entries[FILE_COUNT];
    unsigned count = 0;
    FileResult r = fs->list(parent, 0, entries, FILE_COUNT, &count);
    if (r != FILE_OK) {
        message = files_error(r);
        return 0;
    }
    char name[FILE_NAME] = "note01.txt";
    int found = 0;
    for (unsigned n = 1; n <= 99 && !found; ++n) {
        name[4] = (char)('0' + n / 10);
        name[5] = (char)('0' + n % 10);
        found = 1;
        for (unsigned i = 0; i < count; ++i)
            if (equal(name, entries[i].name))
                found = 0;
    }
    if (!found) {
        message = "Choose a different filename";
        return 0;
    }
    editor_reset(&document, "", 0);
    copy(filename, name, FILE_NAME);
    original[0] = 0;
    open_id = 0;
    editor_parent = parent;
    name_focus = name_selected = discard_pending = selection_drag = 0;
    mark_clean();
    message = "New note - Ctrl+S or F6 to save";
    return 1;
}
int file_app_in_use(unsigned id) {
    if (!initialized || !id)
        return 0;
    if (id == open_id)
        return 1;
    unsigned parent = editor_parent;
    for (unsigned depth = 0; parent && depth < FILE_COUNT; ++depth) {
        if (id == parent)
            return 1;
        FileEntry e;
        if (!fs || fs->entry(parent, &e) != FILE_OK)
            break;
        parent = e.parent;
    }
    return 0;
}
static void save(void) {
    if (!fs) {
        message = "Files unavailable";
        return;
    }
    if (open_id) {
        FileEntry e;
        FileResult r = fs->entry(open_id, &e);
        if (r != FILE_OK || e.flags & (FILE_DIRECTORY | FILE_TRASHED) || e.parent != editor_parent ||
            !equal(e.name, original)) {
            message = "File changed outside Notes - reopen it from Files";
            return;
        }
    }
    FileEntry entries[FILE_COUNT];
    unsigned count = 0;
    FileResult r = fs->list(editor_parent, 0, entries, FILE_COUNT, &count);
    if (r != FILE_OK) {
        message = files_error(r);
        return;
    }
    for (unsigned i = 0; i < count; ++i)
        if (equal(filename, entries[i].name) && entries[i].id != open_id) {
            message = "Name already exists - choose another filename";
            return;
        }
    // Filename edits save a new copy; do not remove the original document.
    r = fs->write_at(editor_parent, filename, text, length);
    if (r != FILE_OK) {
        message = files_error(r);
        return;
    }
    if (fs->list(editor_parent, 0, entries, FILE_COUNT, &count) == FILE_OK)
        for (unsigned i = 0; i < count; ++i)
            if (equal(filename, entries[i].name))
                open_id = entries[i].id;
    copy(original, filename, FILE_NAME);
    mark_clean();
    apps_documents_changed();
    message = "Saved to disk";
}
int file_app_has_changes(void) { return dirty; }
int file_app_save_pending(void) {
    if (dirty)
        save();
    return !dirty;
}
int file_app_close(void) {
    if (dirty) {
        message = "Save or press F7 twice to discard before closing";
        return 0;
    }
    initialized = selection_drag = 0;
    open_id = editor_parent = 0;
    return 1;
}
int file_app_escape(void) { return 0; }
static uint32_t rgb(const BootInfo *s, unsigned r, unsigned g, unsigned b) {
    return color(s, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static GuiRect body_rect(GuiRect w) { return (GuiRect){w.x + 16, w.y + 140, w.width - 32, w.height - 190}; }
static int inside(GuiRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
static void clipped(const BootInfo *s, int x, int y, int width, const char *value) {
    char shown[FILE_PATH];
    unsigned i = 0;
    for (; value[i] && i + 1 < sizeof(shown); ++i) {
        shown[i] = value[i];
        shown[i + 1] = 0;
        if (gui_text_width(shown, GUI_SMALL) > width) {
            shown[i] = 0;
            break;
        }
    }
    shown[i] = 0;
    gui_text(s, x, y, shown, GUI_SMALL, rgb(s, 188, 210, 226));
}

static void draw_text(const BootInfo *screen, GuiRect body) {
    unsigned columns = (unsigned)((body.width - 32) / 12), visible_rows = (unsigned)((body.height - 20) / 24);
    if (!columns || !visible_rows)
        return;
    if (columns > 159)
        columns = 159;
    editor_layout(&document, columns, visible_rows);
    unsigned first = document.scroll, line = 0, column = 0;
    unsigned a = document.cursor < document.anchor ? document.cursor : document.anchor;
    unsigned b = document.cursor > document.anchor ? document.cursor : document.anchor;
    for (unsigned i = 0; i <= length; ++i) {
        if (line >= first && line < first + visible_rows) {
            int x = body.x + 10 + (int)column * 12, y = body.y + 10 + (int)(line - first) * 24;
            if (i >= a && i < b)
                gui_round_rect(screen, (GuiRect){x, y, 12, 23}, 0, rgb(screen, 38, 89, 119), 255);
            if (i < length && text[i] != '\n') {
                char glyph[2] = {text[i] >= 32 && text[i] <= 126 ? text[i] : '?', 0};
                gui_text(screen, x, y, glyph, GUI_MONO, rgb(screen, 219, 231, 239));
            }
            if (i == document.cursor && !name_focus)
                gui_round_rect(screen, (GuiRect){x, y, 2, 21}, 0, rgb(screen, 136, 231, 224), 255);
        }
        if (i < length && (text[i] == '\n' || ++column == columns)) {
            column = 0;
            ++line;
        }
    }
    unsigned total = line + 1;
    if (total > visible_rows) {
        int track = body.height - 20, thumb = (int)((unsigned)track * visible_rows / total);
        if (thumb < 16)
            thumb = 16;
        int top = (int)((unsigned)(track - thumb) * document.scroll / (total - visible_rows));
        gui_round_rect(screen, (GuiRect){body.x + body.width - 10, body.y + 10, 4, track}, 2,
                       rgb(screen, 28, 46, 64), 255);
        gui_round_rect(screen, (GuiRect){body.x + body.width - 10, body.y + 10 + top, 4, thumb}, 2,
                       rgb(screen, 111, 169, 184), 255);
    }
}

void file_app_draw(const BootInfo *s, GuiRect w) {
    GuiRect new_button = {w.x + 16, w.y + 65, 88, 35}, save_button = {w.x + 112, w.y + 65, 88, 35},
            field = {w.x + 212, w.y + 65, w.width - 228, 35}, body = body_rect(w);
    gui_round_rect(s, new_button, 8, rgb(s, 31, 58, 76), 255);
    gui_text(s, new_button.x + 16, new_button.y + 10, "New", GUI_SMALL, rgb(s, 218, 233, 243));
    gui_round_rect(s, save_button, 8, rgb(s, 31, 78, 87), 255);
    gui_text(s, save_button.x + 15, save_button.y + 10, "Save F6", GUI_SMALL, rgb(s, 218, 233, 243));
    gui_round_rect(s, field, 8, rgb(s, 29, 45, 63), 255);
    clipped(s, field.x + 12, field.y + 10, field.width - 24, filename);
    if (name_focus)
        gui_line(s, field.x + 8, field.y + 31, field.x + field.width - 8, field.y + 31, 1,
                 rgb(s, 132, 223, 216));
    char path[FILE_PATH];
    if (fs->path(editor_parent, path, sizeof(path)) != FILE_OK)
        copy(path, "/", sizeof(path));
    clipped(s, w.x + 18, w.y + 112, w.width - 36, path);
    gui_round_rect(s, body, 10, rgb(s, 8, 20, 32), 255);
    draw_text(s, body);
    clipped(s, w.x + 18, w.y + w.height - 30, w.width - 36, message);
}
void file_app_click(GuiRect w, int px, int py) {
    selection_drag = discard_pending = 0;
    if (inside((GuiRect){w.x + 16, w.y + 65, 88, 35}, px, py)) {
        file_app_new(editor_parent);
        return;
    }
    if (inside((GuiRect){w.x + 112, w.y + 65, 88, 35}, px, py)) {
        save();
        return;
    }
    if (inside((GuiRect){w.x + 212, w.y + 65, w.width - 228, 35}, px, py)) {
        name_focus = 1;
        name_selected = 0;
        return;
    }
    GuiRect body = body_rect(w);
    if (inside(body, px, py)) {
        name_focus = 0;
        if (px >= body.x + body.width - 18) {
            unsigned total = editor_line(&document, length) + 1,
                     max = total > document.rows ? total - document.rows : 0;
            unsigned track = (unsigned)(body.height - 20),
                     offset = py > body.y + 10 ? (unsigned)(py - body.y - 10) : 0;
            document.scroll = track ? offset * max / track : 0;
            if (document.scroll > max)
                document.scroll = max;
        } else {
            editor_point(&document,
                         document.scroll + (py > body.y + 10 ? (unsigned)(py - body.y - 10) / 24 : 0),
                         px > body.x + 10 ? (unsigned)(px - body.x - 10) / 12 : 0, 0);
            selection_drag = 1;
        }
    }
}
void file_app_pointer(GuiRect w, int px, int py, unsigned buttons) {
    if (!(buttons & 1)) {
        selection_drag = 0;
        return;
    }
    if (!selection_drag)
        return;
    GuiRect body = body_rect(w);
    int x = px - body.x - 10, y = py - body.y - 10;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if ((unsigned)x / 12 > document.columns)
        x = (int)document.columns * 12;
    if ((unsigned)y / 24 >= document.rows)
        y = (int)(document.rows ? document.rows - 1 : 0) * 24;
    editor_point(&document, document.scroll + (unsigned)y / 24, (unsigned)x / 12, 1);
}
void file_app_key(char key) {
    unsigned command = (unsigned char)key;
    if (key == 7) {
        if (dirty && !discard_pending) {
            discard_pending = 1;
            message = "Press F7 again to discard unsaved changes";
            return;
        }
        discard_pending = 0;
        dirty = 0;
        if (open_id)
            file_app_load(open_id);
        else {
            editor_reset(&document, saved_text, saved_length);
            copy(filename, saved_name, FILE_NAME);
            mark_clean();
        }
        message = "Unsaved changes discarded";
        return;
    }
    discard_pending = 0;
    if (key == 6 || command == EDIT_SAVE) {
        save();
        return;
    }
    if (key == '\t') {
        name_focus = !name_focus;
        return;
    }
    if (name_focus) {
        if (command == EDIT_SELECT_ALL) {
            name_selected = 1;
            return;
        }
        unsigned n = 0;
        while (filename[n])
            ++n;
        if (key == '\b' && n) {
            filename[name_selected ? 0 : n - 1] = 0;
            name_selected = 0;
        } else if (command >= 32 && command <= 126 && n < FILE_NAME - 1) {
            if (name_selected)
                n = 0;
            filename[n] = key;
            filename[n + 1] = 0;
            name_selected = 0;
        }
    } else if (editor_key(&document, command) < 0) {
        message = "Document limit reached; paste was not inserted";
        return;
    }
    update_dirty();
    message = dirty ? "Unsaved changes - Ctrl+S or F6 to save" : "No unsaved changes";
}
