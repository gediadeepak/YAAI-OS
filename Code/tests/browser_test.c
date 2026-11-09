#include "../apps.h"
#include "../edit_keys.h"
#include "../file_app.h"
#include "../file_browser.h"
#include "../volumes.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static uint8_t sectors[1024][512];
static uint64_t addresses[1024];
static unsigned used;
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
    if (lba >= 32768)
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
    CHECK(apps_close(APP_FILES));
    apps_initialize(0);
    files_mount(0);
    return 0;
}
