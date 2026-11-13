#include "../apps.h"
#include "../edit_keys.h"
#include "../file_app.h"
#include "../file_browser.h"
#include "../platform/platform.h"
#include "../settings.h"
#include "../volumes.h"
#define CHECK(x)                                                                                           \
    do {                                                                                                   \
        if (!(x))                                                                                          \
            return __LINE__;                                                                               \
    } while (0)
static uint8_t sectors[1024][512];
static uint64_t addresses[1024];
static unsigned used;
static int fail_write;
static uint32_t canvas_pixels[640 * 480 + 2];
static int read_sector(void *c, uint64_t lba, void *p) {
    (void)c;
    if (lba >= 32768)
        return 0;
    unsigned index = 0;
    while (index < used && addresses[index] != lba)
        ++index;
    for (unsigned i = 0; i < 512; ++i)
        ((uint8_t *)p)[i] = index < used ? sectors[index][i] : 0;
    return 1;
}
static int write_sector(void *c, uint64_t lba, const void *p) {
    (void)c;
    if (fail_write || lba >= 32768)
        return 0;
    unsigned index = 0;
    while (index < used && addresses[index] != lba)
        ++index;
    if (index == used) {
        if (used == 1024)
            return 0;
        addresses[used++] = lba;
    }
    for (unsigned i = 0; i < 512; ++i)
        sectors[index][i] = ((const uint8_t *)p)[i];
    return 1;
}
static int flush(void *c) {
    (void)c;
    return 1;
}
static WindowManager manager;
static void keys(unsigned id, const char *s) {
    while (*s)
        apps_key(id, *s++);
}
static int equal(const char *a, const char *b) {
    unsigned i = 0;
    while (a[i] && a[i] == b[i])
        ++i;
    return a[i] == b[i];
}
int browser_test(void) {
    used = 0;
    uint8_t root[512], id[16] = {2};
    documents_header(root, 32768, id);
    CHECK(write_sector(0, 0, root) && write_sector(0, 1, root));
    BlockDevice disk = {0, 32768, 1, read_sector, write_sector, flush};
    CHECK(files_mount(&disk) == FILE_OK);
    CHECK(files_mkdir(0, "work") == FILE_OK);
    CHECK(files_write("alpha.txt", "alpha", 5) == FILE_OK);
    CHECK(files_write("beta.txt", "beta", 4) == FILE_OK);
    uint8_t binary[2] = {0, 1};
    CHECK(files_write("binary.bin", binary, 2) == FILE_OK);
    unsigned work, alpha, beta, bin;
    CHECK(files_resolve("/work", &work) == FILE_OK && files_resolve("/alpha.txt", &alpha) == FILE_OK);
    CHECK(files_resolve("/beta.txt", &beta) == FILE_OK && files_resolve("/binary.bin", &bin) == FILE_OK);
    windows_initialize(&manager);
    GuiRect w = {0, 0, 900, 600};
    CHECK(windows_register(&manager, APP_NOTES, w) && windows_register(&manager, APP_FILES, w));
    apps_initialize(&manager);
    CHECK(apps_launch(APP_FILES));
    apps_click(APP_FILES, w, 80, 190);
    apps_click(APP_FILES, w, 80, 190);
    CHECK(file_browser_directory() == work);
    apps_click(APP_FILES, w, 40, 122);
    CHECK(!file_browser_directory());
    apps_click(APP_FILES, w, 80, 226);
    apps_click(APP_FILES, w, 80, 226);
    CHECK(manager.active == APP_NOTES && file_app_document_id() == alpha && manager.visible[APP_FILES]);
    apps_key(APP_NOTES, 'x');
    CHECK(file_app_has_changes());
    CHECK(!apps_open_document(beta) && file_app_document_id() == alpha);
    CHECK(apps_launch(APP_FILES));
    apps_click(APP_FILES, w, 80, 226);
    apps_key(APP_FILES, 'r');
    FileEntry e;
    CHECK(files_entry(alpha, &e) == FILE_OK && equal(e.name, "alpha.txt"));
    CHECK(file_browser_status()[0] == 'C');
    apps_key(APP_NOTES, 6);
    CHECK(!file_app_has_changes());
    CHECK(apps_close(APP_NOTES));
    apps_key(APP_FILES, 'r');
    keys(APP_FILES, "renamed.txt\n");
    CHECK(files_entry(alpha, &e) == FILE_OK && equal(e.name, "renamed.txt"));
    apps_key(APP_FILES, (char)EDIT_DOWN);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    CHECK(file_browser_selected() == alpha);
    apps_key(APP_FILES, 'm');
    keys(APP_FILES, "/work\n");
    CHECK(files_entry(alpha, &e) == FILE_OK && e.parent == work);
    apps_click(APP_FILES, w, 80, 190);
    apps_click(APP_FILES, w, 80, 190);
    CHECK(file_browser_directory() == work);
    apps_click(APP_FILES, w, 80, 190);
    apps_click(APP_FILES, w, 80, 190);
    CHECK(file_app_document_id() == alpha);
    CHECK(apps_launch(APP_FILES));
    apps_click(APP_FILES, w, 40, 122);
    apps_click(APP_FILES, w, 80, 190);
    apps_key(APP_FILES, 't');
    CHECK(files_entry(work, &e) == FILE_OK && !(e.flags & FILE_TRASHED) && file_browser_status()[0] == 'C');
    CHECK(apps_close(APP_NOTES));
    apps_key(APP_FILES, 't');
    apps_key(APP_FILES, '\n');
    CHECK(files_entry(work, &e) == FILE_OK && (e.flags & FILE_TRASHED));
    apps_click(APP_FILES, w, 350, 80);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    apps_key(APP_FILES, 'r');
    CHECK(files_entry(work, &e) == FILE_OK && !(e.flags & FILE_TRASHED));
    apps_click(APP_FILES, w, 350, 80);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    CHECK(file_browser_selected() == beta);
    apps_key(APP_FILES, 't');
    apps_key(APP_FILES, '\n');
    apps_click(APP_FILES, w, 350, 80);
    apps_key(APP_FILES, (char)EDIT_DOWN);
    apps_key(APP_FILES, 'p');
    apps_key(APP_FILES, '\n');
    CHECK(files_entry(beta, &e) == FILE_OK);
    keys(APP_FILES, "DELETE\n");
    CHECK(files_entry(beta, &e) == FILE_NOT_FOUND);
    apps_click(APP_FILES, w, 350, 80);
    apps_click(APP_FILES, w, 80, 190);
    for (unsigned i = 0; i < 21; ++i)
        apps_tick();
    apps_click(APP_FILES, w, 80, 190);
    CHECK(!file_browser_directory());
    apps_click(APP_FILES, w, 80, 190);
    CHECK(file_browser_directory() == work);
    CHECK(!apps_open_document(bin));
    CHECK(!file_app_document_id());
    apps_key(APP_FILES, 'f');
    CHECK(manager.active == APP_NOTES);
    apps_key(APP_NOTES, 'n');
    apps_key(APP_NOTES, 6);
    CHECK(!file_app_has_changes());
    unsigned note;
    CHECK(files_resolve("/work/note01.txt", &note) == FILE_OK);
    CHECK(apps_launch(APP_FILES));
    apps_key(APP_FILES, (char)EDIT_DOWN);
    CHECK(file_browser_selected() == note);
    apps_key(APP_FILES, 'n');
    keys(APP_FILES, "drafts\n");
    unsigned drafts;
    CHECK(files_resolve("/work/drafts", &drafts) == FILE_OK);
    CHECK(apps_close(APP_NOTES));
    CHECK(apps_new_document(work));
    apps_key(APP_NOTES, 'z');
    apps_key(APP_NOTES, '\t');
    apps_key(APP_NOTES, (char)EDIT_SELECT_ALL);
    keys(APP_NOTES, "renamed.txt");
    apps_key(APP_NOTES, 6);
    CHECK(file_app_has_changes() && !apps_close(APP_NOTES));
    char out[16];
    unsigned size = sizeof(out);
    CHECK(files_read_id(alpha, out, &size) == FILE_OK && size == 6 && out[0] == 'a' && out[5] == 'x');
    apps_key(APP_NOTES, 7);
    apps_key(APP_NOTES, 7);
    CHECK(apps_close(APP_NOTES));
    // Copy binary data through the context menu, then paste in another folder.
    apps_click(APP_FILES, w, 40, 122);
    apps_pointer(APP_FILES, w, 80, 226, 2);
    apps_pointer(APP_FILES, w, 80, 226, 0);
    CHECK(file_browser_selected() == bin);
    apps_click(APP_FILES, w, 95, 434);
    apps_click(APP_FILES, w, 80, 190);
    apps_click(APP_FILES, w, 80, 190);
    CHECK(file_browser_directory() == work);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    unsigned pasted;
    CHECK(files_resolve("/work/binary.bin", &pasted) == FILE_OK && pasted != bin);
    apps_key(APP_FILES, (char)EDIT_COPY);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    apps_key(APP_FILES, '3');
    CHECK(files_resolve("/work/binary-copy-1.bin", &pasted) == FILE_NOT_FOUND);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    apps_key(APP_FILES, '1');
    CHECK(files_resolve("/work/binary-copy-1.bin", &pasted) == FILE_OK);
    CHECK(files_write("/work/binary.bin", "old", 3) == FILE_OK);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    fail_write = 1;
    apps_key(APP_FILES, '2');
    fail_write = 0;
    size = sizeof(out);
    CHECK(files_read("/work/binary.bin", out, &size) == FILE_OK && size == 3 && out[0] == 'o');
    size = sizeof(out);
    CHECK(files_read_id(bin, out, &size) == FILE_OK && size == 2 && out[0] == 0 && out[1] == 1);
    apps_key(APP_FILES, '2');
    CHECK(files_mount(&disk) == FILE_OK);
    size = sizeof(out);
    CHECK(files_read("/work/binary.bin", out, &size) == FILE_OK && size == 2 && out[0] == 0 && out[1] == 1);
    size = sizeof(out);
    CHECK(files_read("/work/binary-copy-1.bin", out, &size) == FILE_OK && size == 2 && out[0] == 0 &&
          out[1] == 1);
    CHECK(files_write("/work/drafts/nested.txt", "nested", 6) == FILE_OK);
    apps_click(APP_FILES, w, 40, 122);
    apps_click(APP_FILES, w, 80, 190);
    apps_key(APP_FILES, (char)EDIT_COPY);
    apps_key(APP_FILES, '\n');
    CHECK(file_browser_directory() == work);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    CHECK(files_resolve("/work/work", &pasted) == FILE_NOT_FOUND);
    apps_click(APP_FILES, w, 40, 122);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    apps_key(APP_FILES, '1');
    CHECK(files_resolve("/work-copy-1/drafts/nested.txt", &pasted) == FILE_OK);
    size = sizeof(out);
    CHECK(files_read_id(pasted, out, &size) == FILE_OK && size == 6 && out[0] == 'n');
    unsigned copied_folder = file_browser_selected();
    CHECK(copied_folder && copied_folder != work);
    apps_key(APP_FILES, (char)EDIT_CUT);
    apps_click(APP_FILES, w, 80, 190);
    apps_key(APP_FILES, '\n');
    CHECK(file_browser_directory() == work);
    fail_write = 1;
    apps_key(APP_FILES, (char)EDIT_PASTE);
    fail_write = 0;
    CHECK(files_resolve("/work-copy-1", &pasted) == FILE_OK && pasted == copied_folder);
    CHECK(files_resolve("/work/work-copy-1", &pasted) == FILE_NOT_FOUND);
    apps_key(APP_FILES, (char)EDIT_PASTE);
    CHECK(files_mount(&disk) == FILE_OK);
    CHECK(files_resolve("/work-copy-1", &pasted) == FILE_NOT_FOUND);
    CHECK(files_resolve("/work/work-copy-1", &pasted) == FILE_OK && pasted == copied_folder);
    CHECK(files_resolve("/work/work-copy-1/drafts/nested.txt", &pasted) == FILE_OK);
    CHECK(files_resolve("/work/drafts/nested.txt", &pasted) == FILE_OK);
    CHECK(files_mkdir(work, "batch") == FILE_OK);
    CHECK(files_write("/work/batch/binary.bin", "old", 3) == FILE_OK);
    apps_key(APP_FILES, 'u');
    // Select adjacent binary files, then toggle one twice.
    file_browser_click_modified(w, 80, 298, 0);
    file_browser_click_modified(w, 80, 334, INPUT_SHIFT);
    file_browser_click_modified(w, 80, 334, INPUT_CTRL);
    file_browser_click_modified(w, 80, 334, INPUT_CTRL);
    apps_key(APP_FILES, (char)EDIT_COPY);
    apps_click(APP_FILES, w, 80, 190);
    apps_key(APP_FILES, '\n');
    apps_key(APP_FILES, (char)EDIT_PASTE);
    CHECK(equal(file_browser_status(), "1 completed, 1 need attention"));
    CHECK(files_resolve("/work/batch/binary-copy-1.bin", &pasted) == FILE_OK);
    size = sizeof(out);
    CHECK(files_read("/work/batch/binary.bin", out, &size) == FILE_OK && size == 3 && out[0] == 'o');
    apps_key(APP_FILES, '\n');
    apps_key(APP_FILES, (char)EDIT_SELECT_ALL);
    apps_key(APP_FILES, 't');
    apps_key(APP_FILES, '\n');
    CHECK(equal(file_browser_status(), "2 completed, 0 need attention"));
    CHECK(files_resolve("/work/batch/binary.bin", &pasted) == FILE_NOT_FOUND);
    CHECK(files_resolve("/work/batch/binary-copy-1.bin", &pasted) == FILE_NOT_FOUND);
    apps_key(APP_FILES, '\n');
    apps_key(APP_FILES, (char)EDIT_FIND);
    keys(APP_FILES, "NESTED");
    apps_key(APP_FILES, '\t');
    CHECK(file_browser_directory() != 0);
    CHECK(file_browser_selected() != 0);
    CHECK(files_entry(file_browser_selected(), &e) == FILE_OK && equal(e.name, "nested.txt"));

    CHECK(files_write("/find-test.txt", "one One two", 11) == FILE_OK);
    CHECK(files_resolve("/find-test.txt", &pasted) == FILE_OK);
    CHECK(apps_open_document(pasted));
    apps_key(APP_NOTES, (char)EDIT_REPLACE);
    keys(APP_NOTES, "ONE\tz");
    apps_key(APP_NOTES, (char)EDIT_REPLACE_ALL);
    CHECK(file_app_has_changes());
    CHECK(apps_escape(APP_NOTES));
    apps_key(APP_NOTES, (char)EDIT_UNDO);
    CHECK(!file_app_has_changes());
    apps_key(APP_NOTES, (char)EDIT_REDO);
    for (unsigned frame = 0; frame < 81; ++frame)
        apps_tick();
    file_app_tick();
    CHECK(files_mount(&disk) == FILE_OK);
    unsigned draft_size = sizeof(out);
    CHECK(files_draft_read(out, &draft_size) == FILE_TOO_LARGE && draft_size == 43);
    size = sizeof(out);
    CHECK(files_read("/find-test.txt", out, &size) == FILE_OK && size == 11 && out[0] == 'o');
    apps_key(APP_NOTES, 6);
    size = sizeof(out);
    CHECK(files_read("/find-test.txt", out, &size) == FILE_OK && size == 7 && out[0] == 'z' &&
          out[2] == 'z');
    size = sizeof(out);
    CHECK(files_draft_read(out, &size) == FILE_NOT_FOUND);
    CHECK(apps_close(APP_NOTES));
    // Seed a saved draft, then open Notes as it would after a fresh boot.
    char recovery[39] = "YDR";
    for (unsigned i = 0; i < 12; ++i)
        recovery[4 + i] = "recover.txt"[i];
    recovery[36] = 'a';
    recovery[37] = 'b';
    recovery[38] = 'c';
    CHECK(files_draft_write(recovery, sizeof(recovery)) == FILE_OK);
    CHECK(apps_launch(APP_NOTES) && file_app_recovery_pending());
    apps_key(APP_NOTES, 'r');
    CHECK(file_app_has_changes() && !file_app_recovery_pending());
    apps_key(APP_NOTES, 6);
    size = sizeof(out);
    CHECK(files_read("/recover.txt", out, &size) == FILE_OK && size == 3 && out[2] == 'c');
    CHECK(apps_close(APP_NOTES));
    CHECK(apps_close(APP_FILES));
    files_mount(0);
    CHECK(apps_launch(APP_FILES));
    apps_key(APP_FILES, 'n');
    CHECK(equal(file_browser_status(), "Install YAAI OS, then boot the system disk to save files"));
    apps_key(APP_FILES, 'f');
    CHECK(equal(file_browser_status(), "Install YAAI OS, then boot the system disk to save files"));
    CHECK(gui_initialize());
    BootInfo preview = {0};
    preview.width = 640;
    preview.height = 480;
    preview.pixels_per_scanline = 640;
    preview.pixel_format = 1;
    preview.framebuffer = canvas_pixels + 1;
    canvas_pixels[0] = 0x12345678;
    canvas_pixels[640 * 480 + 1] = 0xabcdef01;
    unsigned appearance = settings_current()->color_mode;
    for (unsigned mode = 0; mode < 3; ++mode) {
        settings_color_mode(mode);
        apps_draw(APP_FILES, &preview, (GuiRect){0, 0, 640, 480});
        CHECK(canvas_pixels[0] == 0x12345678 && canvas_pixels[640 * 480 + 1] == 0xabcdef01);
    }
    settings_color_mode(appearance);
    CHECK(apps_close(APP_FILES));
    apps_initialize(0);
    return 0;
}
