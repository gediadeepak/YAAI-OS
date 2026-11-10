#include "file_browser.h"
#include "apps.h"
#include "edit_keys.h"
#include "file_app.h"
#include "gui.h"
#include "screen.h"
#include "settings.h"
#include "shell_ui.h"
static int inside(GuiRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
static const AppFiles *fs;
static FileEntry entries[FILE_COUNT];
static unsigned count, folder, selected, page, last_id, seen_revision;
static uint64_t last_click;
static int trash, trash_context, context_open, dialog, input_selected;
static GuiRect context;
static FileResult storage_result = FILE_UNAVAILABLE;
static char input[FILE_PATH], status_line[160];
static const char *message = "Double-click to open; right-click for actions";
static unsigned previous_buttons;
static const BootInfo *screen;
static unsigned text_length(const char *s) {
    unsigned n = 0;
    while (s[n])
        ++n;
    return n;
}
static void copy(char *to, const char *from, unsigned cap) {
    unsigned i = 0;
    for (; from[i] && i + 1 < cap; ++i)
        to[i] = from[i];
    to[i] = 0;
}
static int equal(const char *a, const char *b) {
    for (unsigned i = 0;; ++i) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
}
static int before(const FileEntry *a, const FileEntry *b) {
    if ((a->flags & FILE_DIRECTORY) != (b->flags & FILE_DIRECTORY))
        return !!(a->flags & FILE_DIRECTORY);
    for (unsigned i = 0; i < FILE_NAME; ++i) {
        if (a->name[i] != b->name[i])
            return (unsigned char)a->name[i] < (unsigned char)b->name[i];
        if (!a->name[i])
            return 0;
    }
    return 0;
}
static void refresh(void) {
    FileResult r = fs->list(folder, trash, entries, FILE_COUNT, &count);
    storage_result = r;
    if (r != FILE_OK) {
        count = 0;
        message = files_error(r);
    }
    for (unsigned i = 1; i < count; ++i) {
        FileEntry e = entries[i];
        unsigned j = i;
        while (j && before(&e, &entries[j - 1])) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = e;
    }
    int found = 0;
    for (unsigned i = 0; i < count; ++i)
        if (entries[i].id == selected)
            found = 1;
    if (!found)
        selected = 0;
    seen_revision = apps_document_revision();
}
static void open_app(const AppServices *services) {
    fs = services->files;
    context_open = dialog = 0;
    previous_buttons = 0;
    last_id = 0;
    refresh();
}
unsigned file_browser_directory(void) { return folder; }
unsigned file_browser_selected(void) { return selected; }
const char *file_browser_status(void) { return message; }
static unsigned rows(GuiRect w) {
    int n = (w.height - 266) / 36;
    return n > 0 ? (unsigned)n : 1;
}
static void navigate(unsigned id, int in_trash, int inherited) {
    folder = id;
    trash = in_trash;
    trash_context = inherited;
    page = selected = last_id = 0;
    context_open = dialog = 0;
    refresh();
    message = inherited ? "Trash contents are read-only; restore the folder first"
                        : "Double-click to open; right-click for actions";
}
static void activate(unsigned id) {
    FileEntry e;
    FileResult r = fs->entry(id, &e);
    if (r != FILE_OK) {
        message = files_error(r);
        return;
    }
    last_id = 0;
    if (e.flags & FILE_DIRECTORY)
        navigate(id, 0, trash_context || trash || !!(e.flags & FILE_TRASHED));
    else if (trash || trash_context) {
        message = "Restore this document before opening it in Notes";
    } else if (!apps_open_document(id))
        message = file_app_has_changes() ? "Save or discard the note already open in Notes"
                                         : "Notes could not open this file; it supports plain ASCII text";
    else
        message = "Opened in Notes";
}
static int protected_entry(void) {
    if (selected && file_app_in_use(selected)) {
        message = "Close this document in Notes before changing it or its folder";
        return 1;
    }
    return 0;
}
static void start_dialog(unsigned mode) {
    if (storage_result != FILE_OK) {
        message = storage_result == FILE_UNAVAILABLE
                      ? "Install YAAI OS, then boot the system disk to save files"
                      : files_error(storage_result);
        return;
    }
    if ((mode != 1 && !selected) || (mode != 1 && protected_entry())) {
        if (!selected && mode != 1)
            message = "Select an item first";
        return;
    }
    if (trash_context && !trash) {
        message = "Restore the parent folder before changing its contents";
        return;
    }
    if ((trash || trash_context) && (mode == 1 || mode == 2 || mode == 3 || mode == 4)) {
        message = "Leave Trash before this operation";
        return;
    }
    dialog = (int)mode;
    context_open = 0;
    input_selected = 1;
    input[0] = 0;
    if (mode == 1)
        copy(input, "new-folder", sizeof(input));
    if (mode == 2) {
        FileEntry e;
        if (fs->entry(selected, &e) == FILE_OK)
            copy(input, e.name, sizeof(input));
    }
    if (mode == 3)
        copy(input, "/", sizeof(input));
}
static void perform(void) {
    if (dialog != 1 && protected_entry()) {
        dialog = 0;
        return;
    }
    FileResult r = FILE_INVALID;
    if (dialog == 1)
        r = fs->mkdir(folder, input);
    else if (dialog == 2)
        r = fs->rename(selected, input);
    else if (dialog == 3) {
        unsigned destination = 0;
        r = fs->resolve(input, &destination);
        if (r == FILE_OK)
            r = fs->move(selected, destination);
    } else if (dialog == 4)
        r = fs->trash(selected);
    else if (dialog == 5) {
        if (!equal(input, "DELETE")) {
            message = "Type DELETE exactly to confirm permanent removal";
            return;
        }
        r = fs->remove(selected);
    }
    if (r == FILE_OK) {
        dialog = 0;
        selected = last_id = 0;
        apps_documents_changed();
        refresh();
        message = "File operation completed";
    } else
        message = files_error(r);
}
static void command(char key) {
    context_open = 0;
    if (key == 'o') {
        if (selected)
            activate(selected);
        return;
    }
    if (key == 'n') {
        start_dialog(1);
        return;
    }
    if (key == 'f') {
        if (storage_result != FILE_OK) {
            message = storage_result == FILE_UNAVAILABLE
                          ? "Install YAAI OS, then boot the system disk to save files"
                          : files_error(storage_result);
            return;
        }
        if (trash || trash_context) {
            message = "Leave Trash before creating a note";
            return;
        }
        if (!apps_new_document(folder))
            message = "Save or discard the open note before creating a new one";
        else
            message = "New note opened in Notes";
        return;
    }
    if (key == 'r' && trash) {
        if (!selected || protected_entry())
            return;
        FileResult r = fs->restore(selected);
        message = files_error(r);
        if (r == FILE_OK) {
            apps_documents_changed();
            selected = 0;
            refresh();
            message = "Restored from Trash";
        }
        return;
    }
    if (key == 'r')
        start_dialog(2);
    if (key == 'm')
        start_dialog(3);
    if (key == 't')
        start_dialog(4);
    if (key == 'p' && trash)
        start_dialog(5);
}
static uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return color(screen, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static uint32_t accent(void) { return shell_palette(screen).hover; }
static void text(int x, int y, const char *s) {
    gui_text(screen, x, y, s, GUI_SMALL, shell_palette(screen).text);
}
static void clipped(int x, int y, int width, const char *s) {
    char out[FILE_PATH];
    unsigned i = 0;
    for (; s[i] && i + 1 < sizeof(out); ++i) {
        out[i] = s[i];
        out[i + 1] = 0;
        if (gui_text_width(out, GUI_SMALL) > width) {
            out[i] = 0;
            break;
        }
    }
    out[i] = 0;
    text(x, y, out);
}
static void append(char *to, const char *s) {
    unsigned n = text_length(to);
    for (unsigned i = 0; s[i] && n < 159; ++i)
        to[n++] = s[i];
    to[n] = 0;
}
static void append_number(char *to, uint64_t n) {
    char tmp[24], out[24];
    unsigned k = 0, i = 0;
    do {
        tmp[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (k)
        out[i++] = tmp[--k];
    out[i] = 0;
    append(to, out);
}
static void button(GuiRect r, const char *s) {
    gui_round_rect(screen, r, 8, shell_palette(screen).surface, 255);
    clipped(r.x + 10, r.y + 10, r.width - 20, s);
}
static void icon(int x, int y, int directory) {
    shell_icon(screen, directory ? 6 : SHELL_ICON_DOCUMENT, (GuiRect){x, y, 24, 24});
}
static void tool_button(GuiRect r, const char *label, unsigned id, int enabled) {
    ShellPalette p = shell_palette(screen);
    gui_round_rect(screen, r, 9, enabled ? p.surface : p.background, 255);
    shell_icon(screen, id, (GuiRect){r.x + 5, r.y + 6, 22, 22});
    gui_text(screen, r.x + 32, r.y + 10, label, GUI_SMALL, enabled ? p.text : p.muted);
}
static GuiRect crumb_rect[FILE_COUNT + 1];
static unsigned crumb_id[FILE_COUNT + 1], crumb_count;
static void breadcrumbs(GuiRect w, int draw) {
    unsigned chain[FILE_COUNT], n = 0, id = folder;
    while (id && n < FILE_COUNT) {
        FileEntry e;
        if (fs->entry(id, &e) != FILE_OK)
            break;
        chain[n++] = id;
        id = e.parent;
    }
    crumb_count = 0;
    int x = w.x + 16, y = w.y + 111, right = w.x + w.width - 16;
    crumb_rect[crumb_count] = (GuiRect){x, y, 68, 28};
    crumb_id[crumb_count++] = 0;
    if (draw)
        button(crumb_rect[0], trash || trash_context ? "Trash" : "Home");
    x += 76;
    unsigned start = n;
    int available = right - x, total = 0;
    for (unsigned i = 0; i < n; ++i) {
        FileEntry e;
        if (fs->entry(chain[i], &e) != FILE_OK)
            break;
        int width = gui_text_width(e.name, GUI_SMALL) + 24;
        if (total + width + 8 > available)
            break;
        total += width + 8;
        start = i + 1;
    }
    if (start < n && draw)
        text(x - 10, y + 9, ">");
    for (unsigned k = start; k; --k) {
        FileEntry e;
        if (fs->entry(chain[k - 1], &e) != FILE_OK)
            break;
        int width = gui_text_width(e.name, GUI_SMALL) + 24;
        if (x + width > right)
            break;
        crumb_rect[crumb_count] = (GuiRect){x, y, width, 28};
        crumb_id[crumb_count++] = e.id;
        if (draw)
            button(crumb_rect[crumb_count - 1], e.name);
        x += width + 8;
    }
}
static GuiRect dialog_rect(GuiRect w) {
    int width = w.width - 48;
    if (width > 520)
        width = 520;
    return (GuiRect){w.x + (w.width - width) / 2, w.y + 72, width, 222};
}
static int row_at(GuiRect w, int x, int y) {
    if (x < w.x + 16 || x >= w.x + w.width - 16 || y < w.y + 181 || y >= w.y + 181 + (int)rows(w) * 36)
        return -1;
    unsigned index = page * rows(w) + (unsigned)(y - w.y - 181) / 36;
    return index < count ? (int)index : -1;
}
static void draw(const BootInfo *s, GuiRect w) {
    screen = s;
    if (seen_revision != apps_document_revision())
        refresh();
    ShellPalette palette = shell_palette(screen);
    gui_round_rect(screen, (GuiRect){w.x + 8, w.y + 55, w.width - 16, w.height - 64}, 12,
                   palette.background, 255);
    unsigned per = rows(w);
    if (page && page * per >= count)
        page = count ? (count - 1) / per : 0;
    if (apps_active() != APP_FILES)
        context_open = 0;
    const char *tools[] = {"Up", "New folder", "New note", "Trash", "Refresh"};
    int widths[] = {54, 118, 118, 90, 90}, left = w.x + 16;
    for (unsigned i = 0; i < 5; ++i) {
        const unsigned ids[] = {SHELL_ICON_UP, SHELL_ICON_NEW_FOLDER, SHELL_ICON_NEW_NOTE, SHELL_ICON_TRASH,
                                SHELL_ICON_REFRESH};
        tool_button((GuiRect){left, w.y + 65, widths[i], 34}, tools[i], ids[i],
                    storage_result == FILE_OK || i == 4);
        left += widths[i] + 8;
    }
    breadcrumbs(w, 1);
    gui_line(screen, w.x + 16, w.y + 150, w.x + w.width - 16, w.y + 150, 1, shell_palette(screen).border);
    gui_round_rect(screen, (GuiRect){w.x + 16, w.y + 152, w.width - 32, 27}, 7, palette.surface, 255);
    text(w.x + 52, w.y + 160, "Name");
    text(w.x + w.width - 226, w.y + 160, "Type");
    text(w.x + w.width - 112, w.y + 160, "Size");
    for (unsigned i = 0; i < per && page * per + i < count; ++i) {
        FileEntry e = entries[page * per + i];
        int y = w.y + 181 + (int)i * 36;
        if (e.id == selected)
            gui_round_rect(screen, (GuiRect){w.x + 16, y, w.width - 32, 34}, 7, accent(), 255);
        icon(w.x + 25, y + 5, !!(e.flags & FILE_DIRECTORY));
        clipped(w.x + 62, y + 11, w.width - 306, e.name);
        text(w.x + w.width - 226, y + 11, e.flags & FILE_DIRECTORY ? "Folder" : "File");
        char size[160] = "";
        if (!(e.flags & FILE_DIRECTORY)) {
            append_number(size, e.size);
            append(size, " B");
        }
        text(w.x + w.width - 112, y + 11, size);
    }
    if (!count) {
        if (storage_result != FILE_OK) {
            clipped(w.x + 28, w.y + 194, w.width - 56,
                    storage_result == FILE_UNAVAILABLE ? "No documents storage is mounted"
                                                       : files_error(storage_result));
            clipped(w.x + 28, w.y + 220, w.width - 56,
                    "Install YAAI OS, then boot with the system VDI attached.");
        } else
            text(w.x + 28, w.y + 201, trash ? "Trash is empty" : "This folder is empty");
    }
    FileUsage u;
    status_line[0] = 0;
    if (fs->usage(&u) == FILE_OK) {
        unsigned limit = u.version == 1 ? 32 : FILE_COUNT;
        append_number(status_line, limit > u.entries ? limit - u.entries : 0);
        append(status_line, " free slots  |  ");
        uint64_t capacity = (uint64_t)(limit > u.folders ? limit - u.folders : 0) * u.max_bytes;
        append_number(status_line, capacity > u.used_bytes ? (capacity - u.used_bytes) / 1024 : 0);
        append(status_line, " KiB payload free  |  ");
        append_number(status_line, u.volume_sectors / 2048);
        append(status_line, " MiB volume");
    } else
        append(status_line, "Documents storage unavailable");
    gui_round_rect(screen, (GuiRect){w.x + 16, w.y + w.height - 88, w.width - 32, 48}, 10, palette.surface,
                   255);
    clipped(w.x + 18, w.y + w.height - 70, w.width - 204, status_line);
    button((GuiRect){w.x + w.width - 172, w.y + w.height - 78, 68, 30}, "Prev");
    button((GuiRect){w.x + w.width - 96, w.y + w.height - 78, 68, 30}, "Next");
    clipped(w.x + 18, w.y + w.height - 30, w.width - 36, message);
    if (context_open) {
        unsigned n = trash ? 2 : 6;
        gui_shadow(screen, context, 10);
        gui_round_rect(screen, context, 10, shell_palette(screen).surface, 255);
        const char *normal[] = {"Open",          "Rename",     "Move to folder",
                                "Move to Trash", "New folder", "New note"};
        const char *deleted[] = {"Restore", "Delete permanently"};
        for (unsigned i = 0; i < n; ++i)
            text(context.x + 14, context.y + 12 + (int)i * 32, trash ? deleted[i] : normal[i]);
    }
    if (dialog) {
        GuiRect p = dialog_rect(w);
        gui_round_rect(screen, (GuiRect){w.x + 1, w.y + 53, w.width - 2, w.height - 54}, 0, rgb(3, 10, 19),
                       180);
        gui_shadow(screen, p, 14);
        gui_round_rect(screen, p, 14, shell_palette(screen).background, 255);
        const char *titles[] = {
            "", "New folder", "Rename item", "Move to folder", "Move to Trash?", "Delete permanently?"};
        text(p.x + 18, p.y + 20, titles[dialog]);
        clipped(p.x + 18, p.y + 53, p.width - 36,
                dialog == 3   ? "Enter a destination folder path (or / for Home)"
                : dialog == 4 ? "You can restore this item from Trash."
                : dialog == 5 ? "Type DELETE. This cannot be undone."
                              : "Letters, numbers, dot, dash and underscore");
        if (dialog != 4) {
            gui_round_rect(screen, (GuiRect){p.x + 18, p.y + 83, p.width - 36, 37}, 7,
                           shell_palette(screen).surface, 255);
            clipped(p.x + 29, p.y + 96, p.width - 58, input);
        }
        clipped(p.x + 18, p.y + 135, p.width - 36, message);
        button((GuiRect){p.x + 18, p.y + 170, 100, 32}, "Cancel");
        button((GuiRect){p.x + p.width - 138, p.y + 170, 120, 32}, "Confirm");
    }
}
static void click(GuiRect w, int x, int y) {
    if (dialog) {
        GuiRect p = dialog_rect(w);
        if (inside(p, x, y) && y >= p.y + 170 && y < p.y + 202) {
            if (x < p.x + 118)
                dialog = 0;
            else if (x >= p.x + p.width - 138)
                perform();
        }
        return;
    }
    if (context_open) {
        if (inside(context, x, y)) {
            unsigned i = (unsigned)(y - context.y - 6) / 32;
            const char *keys = trash ? "rp" : "ormtnf";
            if (y >= context.y + 6 && i < (trash ? 2u : 6u))
                command(keys[i]);
        }
        context_open = 0;
        return;
    }
    int widths[] = {54, 118, 118, 90, 90}, left = w.x + 16;
    for (unsigned i = 0; i < 5; ++i) {
        if (inside((GuiRect){left, w.y + 65, widths[i], 34}, x, y)) {
            if (i == 0) {
                FileEntry e;
                if (folder && fs->entry(folder, &e) == FILE_OK)
                    navigate((e.flags & FILE_TRASHED) ? 0 : e.parent, 0,
                             trash_context && !(e.flags & FILE_TRASHED));
                else
                    navigate(0, 0, 0);
            }
            if (i == 1)
                command('n');
            if (i == 2)
                command('f');
            if (i == 3)
                navigate(0, !trash, !trash);
            if (i == 4)
                refresh();
            return;
        }
        left += widths[i] + 8;
    }
    breadcrumbs(w, 0);
    for (unsigned i = 0; i < crumb_count; ++i)
        if (inside(crumb_rect[i], x, y)) {
            navigate(crumb_id[i], i == 0 && trash_context, trash_context && i != 0);
            return;
        }
    if (inside((GuiRect){w.x + w.width - 172, w.y + w.height - 78, 68, 30}, x, y)) {
        if (page)
            --page;
        return;
    }
    if (inside((GuiRect){w.x + w.width - 96, w.y + w.height - 78, 68, 30}, x, y)) {
        if ((page + 1) * rows(w) < count)
            ++page;
        return;
    }
    int index = row_at(w, x, y);
    if (index >= 0) {
        unsigned id = entries[index].id;
        uint64_t now = apps_ticks();
        selected = id;
        if (id == last_id && now - last_click <= 20) {
            last_id = 0;
            activate(id);
        } else {
            last_id = id;
            last_click = now;
        }
    } else {
        selected = last_id = 0;
    }
}
static void pointer(GuiRect w, int x, int y, unsigned buttons) {
    if (!dialog && (buttons & 2) && !(previous_buttons & 2) && inside(w, x, y)) {
        int index = row_at(w, x, y);
        selected = index >= 0 ? entries[index].id : 0;
        last_id = 0;
        int height = trash ? 76 : 204;
        context = (GuiRect){x, y, 206, height};
        if (context.x + 206 > w.x + w.width - 12)
            context.x = w.x + w.width - 218;
        if (context.y + height > w.y + w.height - 12)
            context.y = w.y + w.height - height - 12;
        if (context.y < w.y + 56)
            context.y = w.y + 56;
        context_open = 1;
    }
    previous_buttons = buttons;
}
static void key(char k) {
    unsigned command_key = (unsigned char)k;
    if (dialog) {
        unsigned n = text_length(input);
        if (command_key == EDIT_SELECT_ALL) {
            input_selected = 1;
            return;
        }
        if (k == '\n') {
            perform();
            return;
        }
        if (k == '\b' && n) {
            input[input_selected ? 0 : n - 1] = 0;
            input_selected = 0;
        } else if (command_key >= 32 && command_key <= 126 && n < sizeof(input) - 1) {
            if (input_selected)
                n = 0;
            input[n] = k;
            input[n + 1] = 0;
            input_selected = 0;
        }
        return;
    }
    if (k == '\n') {
        if (selected)
            activate(selected);
        return;
    }
    if (command_key == EDIT_UP || command_key == EDIT_DOWN) {
        unsigned i = 0;
        while (i < count && entries[i].id != selected)
            ++i;
        if (command_key == EDIT_DOWN) {
            if (i >= count)
                i = 0;
            else if (i + 1 < count)
                ++i;
        } else if (i >= count)
            i = count ? count - 1 : 0;
        else if (i)
            --i;
        if (i < count)
            selected = entries[i].id;
        return;
    }
    if (k == 'u') {
        refresh();
        return;
    }
    command(k);
}
static int escape(void) {
    if (dialog || context_open) {
        dialog = context_open = 0;
        return 1;
    }
    return 0;
}
const AppDefinition browser_application = {APP_FILES, "Files", 640,     360,    open_app, draw,
                                           key,       click,   pointer, escape, 0};
