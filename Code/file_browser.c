#include "file_browser.h"
#include "apps.h"
#include "edit_keys.h"
#include "file_app.h"
#include "gui.h"
#include "platform/platform.h"
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
enum { PASTE_CONFLICT = 6 };
static unsigned previous_buttons, clipboard_size, paste_target;
static char clipboard_name[FILE_NAME];
static uint8_t clipboard_data[FILE_BYTES];
static FileEntry clipboard_root;
static uint64_t extra_selection;
static unsigned selection_anchor, batch_count, batch_revision;
static FileEntry batch_items[FILE_COUNT];
static unsigned batch_status[FILE_COUNT], report_count, report_page;
static char batch_message[160];
enum { BATCH_REPORT = 7, BATCH_TRASH = 8, REPORT_ROWS = 6 };

static uint64_t selection_bit(unsigned id) { return id && id <= FILE_COUNT ? UINT64_C(1) << (id - 1) : 0; }
static uint64_t selection(void) { return extra_selection | selection_bit(selected); }
static unsigned selection_count(void) {
    unsigned total = 0;
    uint64_t bits = selection();
    while (bits) {
        bits &= bits - 1;
        ++total;
    }
    return total;
}
static int clipboard_cut;
static void append(char *to, const char *s);
static void append_number(char *to, uint64_t n);
static FileEntry tree_entries[FILE_COUNT];
static unsigned tree_parent[FILE_COUNT], tree_created[FILE_COUNT];

static int inside_source(unsigned parent, unsigned source) {
    for (unsigned depth = 0; parent && depth < FILE_COUNT; ++depth) {
        if (parent == source)
            return 1;
        FileEntry entry;
        if (fs->entry(parent, &entry) != FILE_OK)
            return 1;
        parent = entry.parent;
    }
    return parent != 0;
}
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
    uint64_t visible = 0;
    for (unsigned i = 0; i < count; ++i)
        visible |= selection_bit(entries[i].id);
    extra_selection &= visible;
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
    page = selected = last_id = selection_anchor = 0;
    extra_selection = 0;
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
static const FileEntry *named_entry(const char *name) {
    for (unsigned i = 0; i < count; ++i)
        if (equal(entries[i].name, name))
            return &entries[i];
    return 0;
}

static FileResult find_child(unsigned parent, const char *name, unsigned *id) {
    for (unsigned candidate = 1; candidate <= FILE_COUNT; ++candidate) {
        FileEntry entry;
        FileResult result = fs->entry(candidate, &entry);
        if (result == FILE_IO_ERROR)
            return result;
        if (result == FILE_OK && !(entry.flags & FILE_TRASHED) && entry.parent == parent &&
            equal(entry.name, name)) {
            *id = candidate;
            return FILE_OK;
        }
    }
    return FILE_NOT_FOUND;
}

static FileResult copy_tree(FileEntry root, unsigned destination, const char *name, int *partial) {
    unsigned total = 1;
    unsigned created = 0;
    tree_entries[0] = root;
    for (unsigned node = 0; node < total; ++node) {
        if (!(tree_entries[node].flags & FILE_DIRECTORY))
            continue;
        unsigned children = 0;
        FileEntry children_list[FILE_COUNT];
        FileResult result = fs->list(tree_entries[node].id, 0, children_list, FILE_COUNT, &children);
        if (result != FILE_OK)
            return result;
        if (children > FILE_COUNT - total)
            return FILE_CORRUPT;
        for (unsigned child = 0; child < children; ++child) {
            tree_parent[total] = node;
            tree_entries[total++] = children_list[child];
        }
    }
    FileUsage usage;
    FileResult result = fs->usage(&usage);
    if (result != FILE_OK)
        return result;
    unsigned capacity = usage.version == 1 ? 32 : FILE_COUNT;
    if (total + usage.entries > capacity)
        return FILE_FULL;

    static uint8_t payload[FILE_BYTES];
    for (unsigned node = 0; node < total; ++node) {
        FileEntry *entry = &tree_entries[node];
        unsigned parent = node ? tree_created[tree_parent[node]] : destination;
        const char *child_name = node ? entry->name : name;
        if (entry->flags & FILE_DIRECTORY) {
            result = fs->mkdir(parent, child_name);
        } else {
            unsigned size = sizeof(payload);
            result = fs->read_id(entry->id, payload, &size);
            if (result == FILE_OK)
                result = fs->write_at(parent, child_name, payload, size);
        }
        if (result != FILE_OK)
            break;
        result = find_child(parent, child_name, &tree_created[node]);
        if (result != FILE_OK) {
            *partial = 1;
            break;
        }
        ++created;
    }
    if (result != FILE_OK && created) {
        // Hide the incomplete tree first, then purge children before parents.
        if (fs->trash(tree_created[0]) != FILE_OK) {
            *partial = 1;
        } else {
            while (created)
                if (fs->remove(tree_created[--created]) != FILE_OK)
                    *partial = 1;
        }
    }
    return result;
}

static unsigned capture_selection(void) {
    unsigned total = 0;
    uint64_t bits = selection();
    for (unsigned i = 0; i < count; ++i)
        if (bits & selection_bit(entries[i].id))
            batch_items[total++] = entries[i];
    return total;
}

static void run_batch(int to_trash) {
    unsigned total = to_trash ? capture_selection() : batch_count;
    if (!total || trash || trash_context)
        return;
    if (!to_trash && batch_revision != apps_document_revision()) {
        message = "Files changed. Select and copy or cut the items again.";
        return;
    }
    unsigned done = 0, retained = 0;
    for (unsigned i = 0; i < total; ++i) {
        FileEntry item = batch_items[i];
        FileResult result = FILE_OK;
        int partial = 0;
        if ((to_trash || clipboard_cut) && file_app_in_use(item.id))
            result = FILE_BAD_MOVE;
        else if (to_trash)
            result = fs->trash(item.id);
        else if ((item.flags & FILE_DIRECTORY) && inside_source(folder, item.id))
            result = FILE_BAD_MOVE;
        else if (clipboard_cut && item.parent == folder)
            result = FILE_EXISTS;
        else {
            unsigned existing;
            result = find_child(folder, item.name, &existing);
            if (result == FILE_OK)
                result = FILE_EXISTS;
            else if (result == FILE_NOT_FOUND) {
                if (clipboard_cut)
                    result = fs->move(item.id, folder);
                else if (item.flags & FILE_DIRECTORY)
                    result = copy_tree(item, folder, item.name, &partial);
                else {
                    unsigned size = sizeof(clipboard_data);
                    result = fs->read_id(item.id, clipboard_data, &size);
                    if (result == FILE_OK)
                        result = fs->write_at(folder, item.name, clipboard_data, size);
                }
            }
        }
        batch_status[i] = partial ? FILE_CORRUPT + 100u : (unsigned)result;
        if (result == FILE_OK)
            ++done;
    }
    report_count = total;
    report_page = 0;
    if (done || !to_trash)
        apps_documents_changed();
    refresh();
    selected = last_id = selection_anchor = 0;
    extra_selection = 0;
    batch_message[0] = 0;
    append_number(batch_message, done);
    append(batch_message, " completed, ");
    append_number(batch_message, total - done);
    append(batch_message, " need attention");
    message = batch_message;
    dialog = BATCH_REPORT;
    if (to_trash) {
        // This operation uses the same bounded item list as the batch clipboard.
        batch_count = 0;
    } else if (clipboard_cut) {
        for (unsigned i = 0; i < total; ++i)
            if (batch_status[i] != FILE_OK)
                ++retained;
        // Keep the results intact until dismissed. Re-cut failed items to retry.
        batch_count = 0;
        clipboard_name[0] = 0;
        if (retained)
            append(batch_message, ". Re-cut failed items to retry.");
    } else {
        batch_revision = apps_document_revision();
    }
}

static void copy_document(int cut) {
    if (selection_count() > 1 && !trash && !trash_context) {
        batch_count = capture_selection();
        batch_revision = apps_document_revision();
        clipboard_cut = cut;
        clipboard_name[0] = 0;
        message = cut ? "Items cut. Open a folder and paste to move them."
                      : "Items copied. Open a folder and paste.";
        return;
    }
    if (!selected || trash || trash_context) {
        message = "Select a document outside Trash to copy";
        return;
    }
    FileEntry entry;
    FileResult r = fs->entry(selected, &entry);
    if (r != FILE_OK) {
        message = files_error(r);
        return;
    }
    unsigned size = sizeof(clipboard_data);
    if (!cut && !(entry.flags & FILE_DIRECTORY)) {
        r = fs->read_id(entry.id, clipboard_data, &size);
        if (r != FILE_OK) {
            message = files_error(r);
            return;
        }
    }
    batch_count = 0;
    copy(clipboard_name, entry.name, sizeof(clipboard_name));
    clipboard_root = entry;
    clipboard_cut = cut;
    clipboard_size = size;
    message =
        cut ? "Item cut. Open a folder and paste to move it." : "Item copied. Open a folder and paste.";
}

static void paste_document(int choice) {
    if (batch_count) {
        run_batch(0);
        return;
    }
    if (!clipboard_name[0] || trash || trash_context) {
        message = "Copy a document and open a folder outside Trash";
        return;
    }
    refresh();
    if (storage_result != FILE_OK)
        return;
    int directory = !!(clipboard_root.flags & FILE_DIRECTORY);
    if (directory || clipboard_cut) {
        FileEntry current;
        if (fs->entry(clipboard_root.id, &current) != FILE_OK || current.flags != clipboard_root.flags ||
            current.parent != clipboard_root.parent || !equal(current.name, clipboard_name) ||
            inside_source(current.parent, current.id)) {
            message = "The source changed. Select it and copy or cut again.";
            dialog = 0;
            return;
        }
        if (directory && inside_source(folder, current.id)) {
            message = "A folder cannot be pasted inside itself";
            return;
        }
        if (clipboard_cut && file_app_in_use(current.id)) {
            message = "Close the open document before moving this item";
            return;
        }
        if (clipboard_cut && folder == current.parent) {
            clipboard_name[0] = 0;
            dialog = 0;
            message = "The item is already in this folder";
            return;
        }
    }
    const FileEntry *existing = named_entry(clipboard_name);
    if (!choice && existing) {
        paste_target = existing->id;
        dialog = PASTE_CONFLICT;
        message = clipboard_cut ? "Cancel and rename the source before moving"
                                : "Choose how to handle the existing item";
        return;
    }
    if (choice == 2 && (directory || clipboard_cut))
        return;
    if (choice == 2) {
        if (!existing || existing->id != paste_target) {
            message = "The destination changed. Start the paste again.";
            dialog = 0;
            return;
        }
        if (existing->flags & FILE_DIRECTORY || file_app_in_use(existing->id)) {
            message = "Replace requires a document that is closed in Notes";
            return;
        }
    }

    char name[FILE_NAME];
    copy(name, clipboard_name, sizeof(name));
    if (choice == 1 && clipboard_cut)
        return;
    if (choice == 1) {
        unsigned length = text_length(clipboard_name);
        unsigned extension = length;
        for (unsigned i = 1; i < length; ++i)
            if (!directory && clipboard_name[i] == '.')
                extension = i;
        // Leave room for the suffix and preserve short file extensions.
        unsigned extension_length = length - extension;
        if (extension_length > 12) {
            extension = length;
            extension_length = 0;
        }
        unsigned stem = FILE_NAME - 1 - 8 - extension_length;
        if (stem > extension)
            stem = extension;
        for (unsigned number = 1; number <= FILE_COUNT + 1; ++number) {
            for (unsigned i = 0; i < stem; ++i)
                name[i] = clipboard_name[i];
            copy(name + stem, "-copy-", sizeof(name) - stem);
            unsigned end = stem + 6;
            if (number >= 10)
                name[end++] = (char)('0' + number / 10);
            name[end++] = (char)('0' + number % 10);
            copy(name + end, clipboard_name + extension, sizeof(name) - end);
            if (!named_entry(name))
                break;
        }
    }
    int partial = 0;
    FileResult result;
    if (clipboard_cut)
        result = fs->move(clipboard_root.id, folder);
    else if (directory)
        result = copy_tree(clipboard_root, folder, name, &partial);
    else
        result = fs->write_at(folder, name, clipboard_data, clipboard_size);
    if (result != FILE_OK) {
        if (partial) {
            apps_documents_changed();
            refresh();
            dialog = 0;
        }
        message = partial ? "Copy failed. An incomplete copy may remain." : files_error(result);
        return;
    }
    dialog = 0;
    last_id = 0;
    apps_documents_changed();
    refresh();
    const FileEntry *pasted = named_entry(name);
    if (pasted)
        selected = pasted->id;
    message = clipboard_cut ? "Item moved" : "Item pasted";
    if (clipboard_cut)
        clipboard_name[0] = 0;
}

static void command(char key) {
    context_open = 0;
    if ((unsigned char)key == EDIT_COPY) {
        copy_document(0);
        return;
    }
    if ((unsigned char)key == EDIT_CUT) {
        copy_document(1);
        return;
    }
    if ((unsigned char)key == EDIT_PASTE) {
        paste_document(0);
        return;
    }
    if (selection_count() > 1 && (key == 'o' || key == 'm' || key == 'r' || key == 'p')) {
        message = "Select one item for this action";
        return;
    }
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
    if (key == 't') {
        if (selection_count() > 1 && !trash && !trash_context)
            dialog = BATCH_TRASH;
        else
            start_dialog(4);
    }
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
static GuiRect paste_button(GuiRect panel, int index) {
    return (GuiRect){panel.x + 18 + index * 110, panel.y + 170, 100, 32};
}
enum { SEARCH_DIALOG = 9, SEARCH_ROWS = 5 };
static char search_query[FILE_NAME];
static FileEntry search_results[FILE_COUNT];
static unsigned search_count, search_index;

static char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c; }

static int name_contains(const char *name, const char *query) {
    for (; *name; ++name) {
        unsigned n = 0;
        while (query[n] && name[n] && lower_ascii(query[n]) == lower_ascii(name[n]))
            ++n;
        if (!query[n])
            return 1;
    }
    return 0;
}

static void search_files(void) {
    search_count = search_index = 0;
    if (!search_query[0])
        return;
    for (unsigned id = 1; id <= FILE_COUNT; ++id) {
        FileEntry item;
        FileResult r = fs->entry(id, &item);
        if (r == FILE_IO_ERROR || r == FILE_UNAVAILABLE) {
            search_count = 0;
            message = files_error(r);
            return;
        }
        if (r != FILE_OK || item.flags & (FILE_DIRECTORY | FILE_TRASHED) ||
            !name_contains(item.name, search_query))
            continue;
        unsigned parent = item.parent;
        for (unsigned depth = 0; parent && depth < FILE_COUNT; ++depth) {
            FileEntry ancestor;
            if (fs->entry(parent, &ancestor) != FILE_OK || ancestor.flags & FILE_TRASHED)
                break;
            parent = ancestor.parent;
        }
        if (!parent)
            search_results[search_count++] = item;
    }
    message = search_count ? "Enter opens the file. Tab opens its folder." : "No matching files";
}

static void search_activate(int open_file) {
    if (!search_count)
        return;
    FileEntry item = search_results[search_index];
    if (open_file) {
        if (apps_open_document(item.id))
            dialog = 0;
        else
            message = "Could not open file in Notes";
    } else {
        navigate(item.parent, 0, 0);
        selected = selection_anchor = item.id;
    }
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
        if (selection() & selection_bit(e.id))
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
    if (selection_count()) {
        append_number(status_line, selection_count());
        append(status_line, " selected  |  ");
    }
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
        unsigned n = trash ? 2 : 9;
        gui_shadow(screen, context, 10);
        gui_round_rect(screen, context, 10, shell_palette(screen).surface, 255);
        const char *normal[] = {"Open",          "Rename",     "Move to folder",
                                "Move to Trash", "New folder", "New note",
                                "Copy",          "Paste",      "Cut"};
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
        if (dialog == PASTE_CONFLICT) {
            text(p.x + 18, p.y + 20, "An item already has this name");
            clipped(p.x + 18, p.y + 53, p.width - 36, clipboard_name);
            clipped(p.x + 18, p.y + 135, p.width - 36, message);
            if (!clipboard_cut)
                button(paste_button(p, 0), "1 Keep both");
            if (!clipboard_cut && !(clipboard_root.flags & FILE_DIRECTORY))
                button(paste_button(p, 1), "2 Replace");
            button(paste_button(p, 2), "3 Cancel");
            return;
        }
        if (dialog == SEARCH_DIALOG) {
            clipped(p.x + 18, p.y + 12, p.width - 36, "Find by filename");
            button((GuiRect){p.x + 18, p.y + 32, p.width - 36, 27},
                   search_query[0] ? search_query : "Type part of a filename");
            unsigned start = search_index / SEARCH_ROWS * SEARCH_ROWS;
            for (unsigned row = 0; row < SEARCH_ROWS && start + row < search_count; ++row) {
                FileEntry *item = &search_results[start + row];
                GuiRect line = {p.x + 18, p.y + 64 + (int)row * 20, p.width - 36, 19};
                if (start + row == search_index)
                    gui_round_rect(screen, line, 4, accent(), 255);
                clipped(line.x + 4, line.y + 2, line.width / 2 - 8, item->name);
                char path[FILE_PATH];
                if (fs->path(item->parent, path, sizeof(path)) == FILE_OK)
                    clipped(line.x + line.width / 2, line.y + 2, line.width / 2 - 4, path);
            }
            button(paste_button(p, 0), "Open");
            button(paste_button(p, 1), "Go to folder");
            button(paste_button(p, 2), "Close");
            clipped(p.x + 18, p.y + 205, p.width - 36, message);
            return;
        }
        if (dialog == BATCH_REPORT) {
            clipped(p.x + 18, p.y + 15, p.width - 36, batch_message);
            for (unsigned row = 0; row < REPORT_ROWS && report_page + row < report_count; ++row) {
                unsigned i = report_page + row;
                unsigned status = batch_status[i];
                const char *result = status == FILE_OK ? "Done"
                                     : status > 100    ? "Incomplete copy may remain"
                                                       : files_error((FileResult)status);
                clipped(p.x + 18, p.y + 43 + (int)row * 20, p.width / 2 - 25, batch_items[i].name);
                clipped(p.x + p.width / 2, p.y + 43 + (int)row * 20, p.width / 2 - 18, result);
            }
            if (report_page)
                button(paste_button(p, 0), "Prev");
            if (report_page + REPORT_ROWS < report_count)
                button(paste_button(p, 1), "Next");
            button(paste_button(p, 2), "Close");
            return;
        }
        if (dialog == BATCH_TRASH) {
            text(p.x + 18, p.y + 20, "Move selected items to Trash?");
            button(paste_button(p, 0), "Cancel");
            button(paste_button(p, 1), "Move to Trash");
            return;
        }
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
void file_browser_click_modified(GuiRect w, int x, int y, unsigned modifiers) {
    if (dialog) {
        GuiRect p = dialog_rect(w);
        if (dialog == PASTE_CONFLICT) {
            if (inside(paste_button(p, 0), x, y))
                paste_document(1);
            else if (inside(paste_button(p, 1), x, y))
                paste_document(2);
            else if (inside(paste_button(p, 2), x, y))
                dialog = 0;
            return;
        }
        if (dialog == SEARCH_DIALOG) {
            if (inside(paste_button(p, 0), x, y))
                search_activate(1);
            else if (inside(paste_button(p, 1), x, y))
                search_activate(0);
            else if (inside(paste_button(p, 2), x, y))
                dialog = 0;
            else if (x >= p.x + 18 && x < p.x + p.width - 18 && y >= p.y + 64 &&
                     y < p.y + 64 + SEARCH_ROWS * 20) {
                unsigned i = search_index / SEARCH_ROWS * SEARCH_ROWS + (unsigned)(y - p.y - 64) / 20;
                if (i < search_count)
                    search_index = i;
            }
            return;
        }
        if (dialog == BATCH_REPORT) {
            if (inside(paste_button(p, 0), x, y) && report_page)
                report_page -= REPORT_ROWS;
            if (inside(paste_button(p, 1), x, y) && report_page + REPORT_ROWS < report_count)
                report_page += REPORT_ROWS;
            if (inside(paste_button(p, 2), x, y))
                dialog = 0;
            return;
        }
        if (dialog == BATCH_TRASH) {
            if (inside(paste_button(p, 0), x, y))
                dialog = 0;
            if (inside(paste_button(p, 1), x, y))
                run_batch(1);
            return;
        }
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
            if (y >= context.y + 6 && i < (trash ? 2u : 9u)) {
                if (!trash && i == 6)
                    command((char)EDIT_COPY);
                else if (!trash && i == 8)
                    command((char)EDIT_CUT);
                else if (!trash && i == 7)
                    command((char)EDIT_PASTE);
                else
                    command(keys[i]);
            }
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
        if (modifiers & INPUT_SHIFT) {
            unsigned anchor = (unsigned)index;
            for (unsigned i = 0; i < count; ++i)
                if (entries[i].id == selection_anchor)
                    anchor = i;
            extra_selection = 0;
            unsigned first = anchor < (unsigned)index ? anchor : (unsigned)index;
            unsigned last = anchor > (unsigned)index ? anchor : (unsigned)index;
            for (unsigned i = first; i <= last; ++i)
                extra_selection |= selection_bit(entries[i].id);
            selected = id;
            last_id = 0;
            return;
        }
        if (modifiers & INPUT_CTRL) {
            extra_selection = selection() ^ selection_bit(id);
            selected = (extra_selection & selection_bit(id)) ? id : 0;
            if (!selected)
                for (unsigned i = 0; i < count; ++i)
                    if (extra_selection & selection_bit(entries[i].id)) {
                        selected = entries[i].id;
                        break;
                    }
            selection_anchor = id;
            last_id = 0;
            return;
        }
        extra_selection = 0;
        selected = selection_anchor = id;
        if (id == last_id && now - last_click <= 20) {
            last_id = 0;
            activate(id);
        } else {
            last_id = id;
            last_click = now;
        }
    } else {
        extra_selection = 0;
        selected = last_id = selection_anchor = 0;
    }
}
static void click(GuiRect w, int x, int y) { file_browser_click_modified(w, x, y, 0); }
static void pointer(GuiRect w, int x, int y, unsigned buttons) {
    if (!dialog && (buttons & 2) && !(previous_buttons & 2) && inside(w, x, y)) {
        int index = row_at(w, x, y);
        if (index >= 0 && !(selection() & selection_bit(entries[index].id))) {
            extra_selection = 0;
            selected = selection_anchor = entries[index].id;
        } else if (index < 0 && selection_count() <= 1) {
            selected = 0;
            extra_selection = 0;
        }
        last_id = 0;
        int height = trash ? 76 : 300;
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
    if (command_key == EDIT_FIND && (!dialog || dialog == SEARCH_DIALOG)) {
        context_open = 0;
        dialog = SEARCH_DIALOG;
        search_query[0] = 0;
        search_files();
        message = "Searches filenames across all folders";
        return;
    }
    if (dialog == SEARCH_DIALOG) {
        unsigned n = text_length(search_query);
        if (k == '\n')
            search_activate(1);
        else if (k == '\t')
            search_activate(0);
        else if (command_key == EDIT_UP && search_index)
            --search_index;
        else if (command_key == EDIT_DOWN && search_index + 1 < search_count)
            ++search_index;
        else if (k == '\b' && n) {
            search_query[n - 1] = 0;
            search_files();
        } else if (command_key == EDIT_SELECT_ALL) {
            search_query[0] = 0;
            search_files();
        } else if (command_key >= 32 && command_key <= 126 && n + 1 < sizeof(search_query)) {
            search_query[n] = k;
            search_query[n + 1] = 0;
            search_files();
        }
        return;
    }
    if (dialog == BATCH_REPORT) {
        if (command_key == EDIT_LEFT && report_page)
            report_page -= REPORT_ROWS;
        if (command_key == EDIT_RIGHT && report_page + REPORT_ROWS < report_count)
            report_page += REPORT_ROWS;
        if (k == '\n')
            dialog = 0;
        return;
    }
    if (dialog == BATCH_TRASH) {
        if (k == '\n')
            run_batch(1);
        return;
    }
    if (dialog == PASTE_CONFLICT) {
        if (k == '1')
            paste_document(1);
        else if (k == '2')
            paste_document(2);
        else if (k == '3')
            dialog = 0;
        return;
    }
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
        if (i < count) {
            extra_selection = 0;
            selected = selection_anchor = entries[i].id;
        }
        return;
    }
    if (command_key == EDIT_SELECT_ALL) {
        extra_selection = 0;
        for (unsigned i = 0; i < count; ++i)
            extra_selection |= selection_bit(entries[i].id);
        selected = selection_anchor = count ? entries[0].id : 0;
        last_id = 0;
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
