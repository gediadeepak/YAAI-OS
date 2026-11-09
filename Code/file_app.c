#include "file_app.h"
#include "editor.h"
#include "apps.h"
static const AppFiles *fs;
void file_app_bind(const AppServices *services){fs=services->files;}
#include "files.h"
#include "screen.h"
static FileEntry entries[FILE_COUNT];
static unsigned count, page;
static Editor document;
#define text document.text
#define length document.length
static char filename[FILE_NAME] = "hello.txt", original[FILE_NAME], incoming[FILE_BYTES + 1];
static char saved_text[FILE_BYTES + 1], saved_name[FILE_NAME];
static unsigned saved_length;
static int initialized, dirty, name_focus, discard_pending;
static int selection_drag;
static unsigned folder, editor_parent, open_id, selected_id;
static int trash_view, trash_context, editor_readonly, dialog, dialog_selectall;
static char dialog_input[FILE_PATH], location[FILE_PATH] = "/", usage_text[96];
static FileUsage usage;
static const char *message = "Select a document or start typing";
static unsigned string_length(const char *s) {
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}
static void copy_name(char *to, const char *from) {
    unsigned i = 0;
    for (; from[i] && i < FILE_NAME - 1; ++i)
        to[i] = from[i];
    to[i] = 0;
}
static void mark_clean(void) {
    for (unsigned i = 0; i <= length; ++i)
        saved_text[i] = text[i];
    saved_length = length;
    copy_name(saved_name, filename);
    dirty = 0;
}
static void update_dirty(void) {
    dirty = length != saved_length;
    for (unsigned i = 0; !dirty && i < length; ++i)
        dirty = text[i] != saved_text[i];
    for (unsigned i = 0; !dirty && i < FILE_NAME; ++i) {
        dirty = filename[i] != saved_name[i];
        if (!filename[i])
            break;
    }
}
static int equal_name(const char *a, const char *b) {
    for (unsigned i = 0; i < FILE_NAME; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return 0;
        if (!ca)
            return 1;
    }
    return 0;
}
static void refresh(void) {
    FileResult result = fs->list(folder, trash_view, entries, FILE_COUNT, &count);
    if (result != FILE_OK) {
        count = 0;
        message = files_error(result);
    }
    fs->usage(&usage);
    if (trash_view)
        copy_name(location, "Trash");
    else if (fs->path(folder, location, sizeof(location)) != FILE_OK)
        copy_name(location, "/");
}
static void open_file_id(unsigned id) {
    if (dirty) {
        message = "Save or press F7 twice to discard your changes";
        return;
    }
    FileEntry entry;
    FileResult result = fs->entry(id, &entry);
    if (result != FILE_OK) {
        message = files_error(result);
        return;
    }
    unsigned size = FILE_BYTES;
    result = fs->read_id(id, incoming, &size);
    if (result != FILE_OK) {
        message = files_error(result);
        return;
    }
    editor_reset(&document, incoming, size);
    copy_name(filename, entry.name);
    copy_name(original, entry.name);
    mark_clean();
    open_id = selected_id = id;
    editor_parent = entry.parent;
    editor_readonly = trash_context || trash_view || !!(entry.flags & FILE_TRASHED);
    name_focus = dirty = 0;
    message = "Opened from disk";
}
void file_app_open(void) {
    refresh();
    if (initialized)
        return; // Keep caret, selection and undo history when reopening the window.
    initialized = 1;
    editor_reset(&document, "", 0);
    mark_clean();
    unsigned id;
    if (fs->resolve("hello.txt", &id) == FILE_OK)
        open_file_id(id);
    else
        for (unsigned i = 0; i < count; ++i)
            if (!(entries[i].flags & FILE_DIRECTORY)) {
                open_file_id(entries[i].id);
                break;
            }
}
static void save(void) {
    if (editor_readonly) {
        message = "Restore this document before editing";
        return;
    }
    if (!dirty && original[0] && equal_name(original, filename)) {
        message = "No unsaved changes";
        return;
    }
    FileEntry destination[FILE_COUNT];
    unsigned destination_count = 0;
    FileResult listed = fs->list(editor_parent, 0, destination, FILE_COUNT, &destination_count);
    if (listed != FILE_OK) {
        message = files_error(listed);
        return;
    }
    for (unsigned i = 0; i < destination_count; ++i)
        if (equal_name(destination[i].name, filename) && destination[i].id != open_id) {
            message = "That name exists; open that document to edit it";
            return;
        }
    FileResult result = fs->write_at(editor_parent, filename, text, length);
    message = files_error(result);
    if (result == FILE_OK) {
        mark_clean();
        copy_name(original, filename);
        refresh();
        for (unsigned i = 0; i < count; ++i)
            if (equal_name(entries[i].name, filename) && entries[i].parent == editor_parent)
                open_id = selected_id = entries[i].id;
        message = "Saved to disk";
    }
}
int file_app_has_changes(void) { return dirty; }
int file_app_close(void){
    if(dirty){message="Save or discard your note before closing";return 0;}
    initialized=dialog=selection_drag=trash_view=trash_context=editor_readonly=0;
    folder=editor_parent=open_id=selected_id=page=0;original[0]=0;copy_name(filename,"hello.txt");
    editor_reset(&document,"",0);mark_clean();return 1;
}
int file_app_save_pending(void) {
    if (dirty)
        save();
    return !dirty;
}
static void new_file(void) {
    if (trash_view || trash_context) {
        message = "Leave Trash before creating documents";
        return;
    }
    if (dirty) {
        message = "Save or press F7 twice to discard your changes";
        return;
    }
    for (unsigned candidate = 1; candidate <= 99; ++candidate) {
        char name[FILE_NAME] = "note01.txt";
        name[4] = (char)('0' + candidate / 10);
        name[5] = (char)('0' + candidate % 10);
        unsigned i = 0;
        for (; i < count && !equal_name(name, entries[i].name); ++i) {
        }
        if (i == count) {
            copy_name(filename, name);
            break;
        }
    }
    original[0] = 0;
    open_id = selected_id = 0;
    editor_parent = folder;
    editor_readonly = 0;
    editor_reset(&document, "", 0);
    mark_clean();
    name_focus = 1;
    message = "Tab switches filename/text; F6 saves";
}
static void navigate(unsigned parent, int trash, int context) {
    if (dirty) {
        message = "Save or discard your document before changing folders";
        return;
    }
    folder = parent;
    trash_view = trash;
    trash_context = context;
    page = 0;
    selected_id = trash ? 0 : parent;
    open_id = 0;
    editor_parent = parent;
    editor_readonly = context || trash;
    original[0] = 0;
    copy_name(filename, "untitled.txt");
    editor_reset(&document, "", 0);
    mark_clean();
    refresh();
    message = trash ? "Trash: Actions lets you restore or permanently delete"
                    : "Choose a document or create a new one";
}
int file_app_escape(void) {
    if (!dialog)
        return 0;
    dialog = 0;
    return 1;
}
static void start_dialog(int mode) {
    if (dirty) {
        message = "Save or discard your document before file operations";
        return;
    }
    if (mode == 4 && !selected_id) {
        message = "Open a file or folder first";
        return;
    }
    dialog = mode;
    dialog_selectall = 0;
    dialog_input[0] = 0;
    selection_drag = 0;
    if (mode == 2) {
        FileEntry e;
        if (fs->entry(selected_id, &e) == FILE_OK)
            copy_name(dialog_input, e.name);
    }
    if (mode == 3) {
        dialog_input[0] = '/';
        dialog_input[1] = 0;
    }
}
static void perform_dialog(void) {
    FileResult r = FILE_INVALID;
    FileEntry e;
    if (dialog == 1)
        r = fs->mkdir(folder, dialog_input);
    else if (dialog == 2)
        r = fs->rename(selected_id, dialog_input);
    else if (dialog == 3) {
        unsigned destination;
        r = fs->resolve(dialog_input, &destination);
        if (r == FILE_OK)
            r = fs->move(selected_id, destination);
    } else if (dialog == 5)
        r = fs->trash(selected_id);
    else if (dialog == 6)
        r = fs->restore(selected_id);
    else if (dialog == 7) {
        if (!equal_name(dialog_input, "DELETE")) {
            message = "Type DELETE to permanently remove this item";
            return;
        }
        r = fs->remove(selected_id);
    }
    if (r != FILE_OK) {
        message = files_error(r);
        return;
    }
    int operation = dialog;
    dialog = 0;
    if (operation >= 5 && folder == selected_id) {
        navigate(0, operation == 5, operation == 5);
    } else {
        refresh();
        if (open_id == selected_id) {
            if (fs->entry(open_id, &e) == FILE_OK) {
                copy_name(filename, e.name);
                copy_name(original, e.name);
                editor_parent = e.parent;
                mark_clean();
            }
            if (operation >= 5) {
                open_id = 0;
                original[0] = 0;
                editor_reset(&document, "", 0);
                mark_clean();
            }
        }
    }
    message = operation == 5   ? "Moved to Trash"
              : operation == 6 ? "Restored"
              : operation == 7 ? "Permanently deleted"
                               : "File operation complete";
}
static void action_key(char key) {
    if (!selected_id) {
        message = "Open a file or folder first";
        dialog = 0;
        return;
    }
    if (key == 'r' || key == 'R')
        start_dialog(2);
    else if (key == 'm' || key == 'M')
        start_dialog(3);
    else if (key == 'd' || key == 'D')
        start_dialog(5);
    else if (key == 't' || key == 'T')
        start_dialog(6);
    else if (key == 'p' || key == 'P')
        start_dialog(7);
}
static uint32_t rgb(const BootInfo *screen, unsigned r, unsigned g, unsigned b) {
    return color(screen, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static void label(const BootInfo *screen, int x, int y, const char *value, unsigned r, unsigned g,
                  unsigned b) {
    gui_text(screen, x, y, value, GUI_SMALL, rgb(screen, r, g, b));
}
static void clipped(const BootInfo *screen, int x, int y, int width, const char *value) {
    char visible[160];
    unsigned i = 0;
    for (; value[i] && i < sizeof(visible) - 1; ++i) {
        visible[i] = value[i];
        visible[i + 1] = 0;
        if (gui_text_width(visible, GUI_SMALL) > width) {
            visible[i] = 0;
            break;
        }
    }
    visible[i] = 0;
    label(screen, x, y, visible, 182, 204, 222);
}
enum {
    ICON_FILE = 1,
    ICON_SAVE,
    ICON_FOLDER,
    ICON_MORE,
    ICON_TRASH,
    ICON_UP,
    ICON_RENAME,
    ICON_MOVE,
    ICON_RESTORE,
    ICON_CANCEL,
    ICON_CONFIRM,
    ICON_PREV,
    ICON_NEXT,
    ICON_FOLDER_ITEM,
    ICON_DOCUMENT
};
static void file_icon(const BootInfo *screen, int x, int y, int kind) {
    uint32_t ink = rgb(screen, 164, 224, 225), dark = rgb(screen, 24, 49, 65),
             gold = rgb(screen, 224, 193, 126);
    if (kind == ICON_FOLDER || kind == ICON_FOLDER_ITEM) {
        gui_round_rect(screen, (GuiRect){x + 1, y + 1, 7, 5}, 2, rgb(screen, 188, 154, 87), 255);
        gui_round_rect(screen, (GuiRect){x, y + 4, 16, 11}, 3, gold, 255);
        if (kind == ICON_FOLDER) {
            gui_line(screen, x + 11, y + 8, x + 11, y + 14, 2, dark);
            gui_line(screen, x + 8, y + 11, x + 14, y + 11, 2, dark);
        }
    } else if (kind == ICON_FILE || kind == ICON_DOCUMENT) {
        gui_round_rect(screen, (GuiRect){x + 2, y, 12, 16}, 2, ink, 255);
        gui_line(screen, x + 5, y + 5, x + 10, y + 5, 1, dark);
        if (kind == ICON_FILE) {
            gui_line(screen, x + 8, y + 8, x + 8, y + 13, 2, dark);
            gui_line(screen, x + 5, y + 11, x + 11, y + 11, 2, dark);
        } else {
            gui_line(screen, x + 5, y + 8, x + 10, y + 8, 1, dark);
            gui_line(screen, x + 5, y + 11, x + 9, y + 11, 1, dark);
        }
    } else if (kind == ICON_SAVE) {
        gui_round_rect(screen, (GuiRect){x, y, 16, 16}, 3, ink, 255);
        gui_round_rect(screen, (GuiRect){x + 3, y, 9, 5}, 1, dark, 255);
        gui_round_rect(screen, (GuiRect){x + 3, y + 9, 10, 7}, 1, dark, 255);
        gui_line(screen, x + 5, y + 12, x + 11, y + 12, 1, ink);
    } else if (kind == ICON_TRASH) {
        uint32_t red = rgb(screen, 230, 157, 154);
        gui_round_rect(screen, (GuiRect){x + 3, y + 5, 10, 11}, 2, red, 255);
        gui_line(screen, x + 1, y + 3, x + 15, y + 3, 2, red);
        gui_line(screen, x + 6, y, x + 10, y, 2, red);
        gui_line(screen, x + 6, y + 7, x + 6, y + 12, 1, dark);
        gui_line(screen, x + 10, y + 7, x + 10, y + 12, 1, dark);
    } else if (kind == ICON_MORE) {
        for (int i = 0; i < 3; ++i)
            gui_round_rect(screen, (GuiRect){x + i * 6, y + 6, 3, 3}, 1, ink, 255);
    } else if (kind == ICON_RENAME) {
        gui_line(screen, x + 3, y + 12, x + 12, y + 3, 4, gold);
        gui_line(screen, x + 2, y + 14, x + 6, y + 13, 2, ink);
        gui_line(screen, x + 10, y + 2, x + 14, y + 6, 1, dark);
    } else if (kind == ICON_CANCEL) {
        gui_line(screen, x + 3, y + 3, x + 13, y + 13, 2, ink);
        gui_line(screen, x + 13, y + 3, x + 3, y + 13, 2, ink);
    } else if (kind == ICON_CONFIRM) {
        gui_line(screen, x + 2, y + 8, x + 6, y + 12, 2, ink);
        gui_line(screen, x + 6, y + 12, x + 14, y + 3, 2, ink);
    } else if (kind == ICON_UP) {
        gui_line(screen, x + 8, y + 14, x + 8, y + 2, 2, ink);
        gui_line(screen, x + 2, y + 8, x + 8, y + 2, 2, ink);
        gui_line(screen, x + 8, y + 2, x + 14, y + 8, 2, ink);
    } else if (kind == ICON_RESTORE) {
        gui_line(screen, x + 2, y + 5, x + 11, y + 5, 2, ink);
        gui_line(screen, x + 11, y + 5, x + 14, y + 8, 2, ink);
        gui_line(screen, x + 14, y + 8, x + 14, y + 13, 2, ink);
        gui_line(screen, x + 2, y + 5, x + 6, y + 1, 2, ink);
        gui_line(screen, x + 2, y + 5, x + 6, y + 9, 2, ink);
    } else {
        int left = kind == ICON_PREV;
        gui_line(screen, x + 2, y + 8, x + 14, y + 8, 2, ink);
        gui_line(screen, x + (left ? 2 : 14), y + 8, x + (left ? 7 : 9), y + 3, 2, ink);
        gui_line(screen, x + (left ? 2 : 14), y + 8, x + (left ? 7 : 9), y + 13, 2, ink);
    }
}
static void button(const BootInfo *screen, GuiRect rect, const char *title) {
    int kind = equal_name(title, "New file")                                      ? ICON_FILE
               : equal_name(title, "Save F6")                                     ? ICON_SAVE
               : equal_name(title, "Folder")                                      ? ICON_FOLDER
               : equal_name(title, "More")                                        ? ICON_MORE
               : equal_name(title, "Up")                                          ? ICON_UP
               : equal_name(title, "Trash") || title[0] == 'D' || title[0] == 'T' ? ICON_TRASH
               : title[0] == 'R' && title[2] == 'n'                               ? ICON_RENAME
               : title[0] == 'R'                                                  ? ICON_RESTORE
               : title[0] == 'M'                                                  ? ICON_MOVE
               : equal_name(title, "Cancel")                                      ? ICON_CANCEL
               : equal_name(title, "Confirm")                                     ? ICON_CONFIRM
               : equal_name(title, "Prev")                                        ? ICON_PREV
                                                                                  : ICON_NEXT;
    gui_round_rect(screen, rect, 9, rgb(screen, 38, 64, 81), 245);
    const char *caption = equal_name(title, "Save F6") ? "Save" : title;
    int tw = gui_text_width(caption, GUI_SMALL);
    if (tw + 32 > rect.width)
        caption = "";
    tw = gui_text_width(caption, GUI_SMALL);
    int total = 16 + (tw ? 6 + tw : 0), start = rect.x + (rect.width - total) / 2;
    file_icon(screen, start, rect.y + (rect.height - 16) / 2, kind);
    if (tw)
        label(screen, start + 22, rect.y + (rect.height - 17) / 2, caption, 211, 231, 236);
}
static unsigned append_number(char *out, unsigned at, uint64_t value) {
    char digits[21];
    unsigned n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        out[at++] = digits[--n];
    out[at] = 0;
    return at;
}
static unsigned append(char *out, unsigned at, const char *s) {
    while (*s)
        out[at++] = *s++;
    out[at] = 0;
    return at;
}
static GuiRect dialog_rect(GuiRect w) {
    int width = w.width - 32;
    if (width > 560)
        width = 560;
    return (GuiRect){w.x + (w.width - width) / 2, w.y + 62, width, 194};
}
static void draw_dialog(const BootInfo *screen, GuiRect window) {
    if (!dialog)
        return;
    GuiRect p = dialog_rect(window);
    gui_shadow(screen, p, 16);
    gui_round_rect(screen, p, 16, rgb(screen, 24, 43, 62), 255);
    const char *titles[] = {"",
                            "New folder",
                            "Rename item",
                            "Move to folder path",
                            "Item actions",
                            "Move item to Trash?",
                            "Restore this item?",
                            "Permanently delete item"};
    label(screen, p.x + 18, p.y + 16, titles[dialog], 227, 237, 246);
    if (dialog == 4) {
        const char *items[] = {"Rename (R)", "Move (M)", "Trash (D)", "Restore (T)", "Delete forever (P)"};
        for (int i = 0; i < 5; ++i) {
            int row = i / 3, col = i % 3;
            button(
                screen,
                (GuiRect){p.x + 14 + col * (p.width - 28) / 3, p.y + 53 + row * 43, (p.width - 40) / 3, 34},
                items[i]);
        }
    } else {
        if (dialog == 1 || dialog == 2 || dialog == 3 || dialog == 7) {
            gui_round_rect(screen, (GuiRect){p.x + 16, p.y + 52, p.width - 32, 36}, 8,
                           dialog_selectall ? rgb(screen, 38, 89, 119) : rgb(screen, 9, 24, 38), 255);
            clipped(screen, p.x + 24, p.y + 62, p.width - 48, dialog_input);
        }
        label(screen, p.x + 18, p.y + 103,
              dialog == 7   ? "Type DELETE; folders must be empty."
              : dialog == 3 ? "Example: /projects   or / for root"
                            : "Enter confirms. Escape cancels.",
              171, 198, 211);
        button(screen, (GuiRect){p.x + 16, p.y + 145, 100, 32}, "Cancel");
        button(screen, (GuiRect){p.x + p.width - 136, p.y + 145, 120, 32}, "Confirm");
    }
}
static int rows(GuiRect window) {
    int value = (window.height - 220) / 42;
    return value > 0 ? value : 1;
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
void file_app_draw(const BootInfo *screen, GuiRect window) {
    int x = window.x, y = window.y;
    button(screen, (GuiRect){x + 16, y + 64, 132, 36}, "New file");
    gui_line(screen, x + 164, y + 62, x + 164, y + window.height - 40, 1, rgb(screen, 43, 62, 80));
    GuiRect field = {x + 184, y + 64, window.width - 304, 36};
    gui_round_rect(screen, field, 9, name_focus ? rgb(screen, 48, 79, 94) : rgb(screen, 29, 43, 61), 255);
    const char *start = filename;
    while (*start && gui_text_width(start, GUI_SMALL) > field.width - 24)
        ++start;
    label(screen, field.x + 10, field.y + 8, start, 220, 230, 235);
    if (dirty)
        label(screen, field.x + field.width - 12, field.y + 8, "*", 226, 199, 136);
    button(screen, (GuiRect){x + window.width - 104, y + 64, 80, 36}, "Save F6");
    unsigned per_page = (unsigned)rows(window);
    if (page && page * per_page >= count)
        page = 0;
    for (unsigned row = 0; row < per_page && page * per_page + row < count; ++row) {
        FileEntry *entry = &entries[page * per_page + row];
        int top = y + 116 + (int)row * 42;
        if (entry->id == selected_id)
            gui_round_rect(screen, (GuiRect){x + 10, top - 3, 144, 28}, 7, rgb(screen, 42, 62, 80), 255);
        file_icon(screen, x + 17, top + 1,
                  (entry->flags & FILE_DIRECTORY) ? ICON_FOLDER_ITEM : ICON_DOCUMENT);
        clipped(screen, x + 40, top, 108, entry->name);
        if (entry->flags & FILE_DIRECTORY)
            label(screen, x + 18, top + 14, "Folder", 125, 171, 181);
        else {
            char size[24];
            unsigned n = append_number(size, 0, entry->size);
            append(size, n, " bytes");
            label(screen, x + 18, top + 14, size, 125, 171, 181);
        }
    }
    if (!count)
        label(screen, x + 18, y + 116, "No documents yet", 139, 165, 183);
    label(screen, x + 18, y + window.height - 90,
          usage.version == 2 ? "64 KiB per file" : "4 KiB legacy volume", 139, 165, 183);
    button(screen, (GuiRect){x + 16, y + window.height - 64, 60, 30}, "Prev");
    button(screen, (GuiRect){x + 84, y + window.height - 64, 64, 30}, "Next");
    const char *tools[] = {"Up", "Folder", "More", "Trash"};
    int widths[] = {48, 76, 76, 76}, left = x + 184;
    for (int i = 0; i < 4; ++i) {
        button(screen, (GuiRect){left, y + 109, widths[i], 30}, tools[i]);
        left += widths[i] + 8;
    }
    clipped(screen, x + 184, y + 143, window.width - 208, location);
    GuiRect body = {x + 184, y + 165, window.width - 208, window.height - 223};
    gui_round_rect(screen, body, 10, rgb(screen, 8, 17, 29), 255);
    draw_text(screen, body);
    unsigned n = append_number(usage_text, 0, usage.entries);
    n = append(usage_text, n, usage.version == 2 ? "/64 items | " : "/32 items | ");
    n = append_number(usage_text, n, usage.used_bytes);
    n = append(usage_text, n, " bytes saved | ");
    n = append_number(usage_text, n, usage.volume_sectors / 2048);
    append(usage_text, n, " MiB volume");
    clipped(screen, x + 184, y + window.height - 50, window.width - 208, usage_text);
    clipped(screen, x + 18, y + window.height - 28, window.width - 36, message);
    draw_dialog(screen, window);
}
void file_app_click(GuiRect window, int px, int py) {
    selection_drag = 0;
    discard_pending = 0;
    int x = px - window.x, y = py - window.y;
    if (dialog) {
        GuiRect p = dialog_rect(window);
        int dx = px - p.x, dy = py - p.y;
        if (dx < 0 || dx >= p.width || dy < 0 || dy >= p.height)
            return;
        if (dialog == 4 && dy >= 53 && dy < 139) {
            int col = (dx - 14) / ((p.width - 28) / 3), row = (dy - 53) / 43, index = row * 3 + col;
            const char keys[] = "rmdtp";
            if (dx >= 14 && col < 3 && index < 5)
                action_key(keys[index]);
        } else if (dy >= 145 && dy < 177) {
            if (dx < 136)
                dialog = 0;
            else if (dx >= p.width - 136)
                perform_dialog();
        }
        return;
    }
    if (x >= 184 && y >= 109 && y < 139) {
        int at = 184;
        int widths[] = {48, 76, 76, 76};
        for (int i = 0; i < 4; ++i) {
            if (x >= at && x < at + widths[i]) {
                if (i == 0) {
                    FileEntry parent;
                    if (!folder)
                        navigate(0, 0, 0);
                    else if (fs->entry(folder, &parent) == FILE_OK)
                        navigate((parent.flags & FILE_TRASHED) ? 0 : parent.parent,
                                 (parent.flags & FILE_TRASHED) != 0, trash_context);
                } else if (i == 1) {
                    if (trash_context || trash_view)
                        message = "Leave Trash before creating folders";
                    else
                        start_dialog(1);
                } else if (i == 2)
                    start_dialog(4);
                else
                    navigate(0, !trash_view, !trash_view);
                return;
            }
            at += widths[i] + 8;
        }
        return;
    }
    if (y >= 64 && y < 100) {
        if (x >= 16 && x < 148)
            new_file();
        else if (x >= window.width - 104 && x < window.width - 24)
            save();
        else if (x >= 184 && x < window.width - 120)
            name_focus = 1;
    } else if (x >= 10 && x < 154) {
        unsigned per_page = (unsigned)rows(window);
        if (y >= window.height - 64 && y < window.height - 34) {
            if (x < 80) {
                if (page)
                    --page;
            } else if ((page + 1) * per_page < count)
                ++page;
        } else if (y >= 113 && y < window.height - 100) {
            unsigned index = page * per_page + (unsigned)(y - 113) / 42;
            if (index < count) {
                FileEntry e = entries[index];
                if (e.flags & FILE_DIRECTORY)
                    navigate(e.id, 0, trash_context || trash_view);
                else
                    open_file_id(e.id);
            }
        }
    } else if (x >= 184 && x < window.width - 24 && y >= 165 && y < window.height - 58) {
        name_focus = 0;
        if (x >= window.width - 40) {
            unsigned total = editor_line(&document, length) + 1,
                     max = total > document.rows ? total - document.rows : 0;
            unsigned offset = y > 175 ? (unsigned)(y - 175) : 0, track = (unsigned)(window.height - 243);
            document.scroll = track ? offset * max / track : 0;
            if (document.scroll > max)
                document.scroll = max;
        } else {
            editor_point(&document, document.scroll + (y > 175 ? (unsigned)(y - 175) / 24 : 0),
                         x > 194 ? (unsigned)(x - 194) / 12 : 0, 0);
            selection_drag = 1;
        }
    }
}
void file_app_pointer(GuiRect window, int px, int py, unsigned buttons) {
    if (!(buttons & 1)) {
        selection_drag = 0;
        return;
    }
    if (!selection_drag || dialog)
        return;
    int x = px - window.x - 194, y = py - window.y - 175;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if ((unsigned)x / 12 > document.columns)
        x = (int)document.columns * 12;
    if ((unsigned)y / 24 >= document.rows)
        y = (int)(document.rows - 1) * 24;
    editor_point(&document, document.scroll + (unsigned)y / 24, (unsigned)x / 12, 1);
}
void file_app_key(char key) {
    unsigned command = (unsigned char)key;
    if (dialog) {
        if (dialog == 4)
            action_key(key);
        else if (command == EDIT_SELECT_ALL)
            dialog_selectall = 1;
        else if (key == '\n')
            perform_dialog();
        else if (dialog == 1 || dialog == 2 || dialog == 3 || dialog == 7) {
            unsigned n = string_length(dialog_input);
            if (key == '\b' && n) {
                if (dialog_selectall)
                    n = 1;
                dialog_input[n - 1] = 0;
                dialog_selectall = 0;
            } else if (command >= 32 && command <= 126 && n < sizeof(dialog_input) - 1) {
                if (dialog_selectall)
                    n = 0;
                dialog_input[n] = key;
                dialog_input[n + 1] = 0;
                dialog_selectall = 0;
            }
        }
        return;
    }
    if (command == 18) {
        start_dialog(4);
        return;
    }
    if (command == 19) {
        if (trash_context || trash_view)
            message = "Leave Trash before creating folders";
        else
            start_dialog(1);
        return;
    }
    if (key == 7) {
        if (dirty && !discard_pending) {
            discard_pending = 1;
            message = "Press F7 again to discard unsaved changes";
            return;
        }
        discard_pending = 0;
        dirty = 0;
        if (original[0])
            open_file_id(open_id);
        else {
            editor_reset(&document, "", 0);
            copy_name(filename, saved_name);
            mark_clean();
            message = "Unsaved changes discarded";
        }
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
    if (editor_readonly) {
        message = "Trash preview is read-only; restore the document first";
        return;
    }
    if (name_focus) {
        unsigned size = string_length(filename);
        if (key == '\b' && size)
            filename[size - 1] = 0;
        else if (command >= 32 && command <= 126 && size < FILE_NAME - 1) {
            filename[size] = key;
            filename[size + 1] = 0;
        }
    } else if (editor_key(&document, command) < 0) {
        message = "Document limit reached; paste was not inserted";
        return;
    }
    update_dirty();
    message = dirty ? "Unsaved changes - Ctrl+S or F6 to save" : "No unsaved changes";
}
