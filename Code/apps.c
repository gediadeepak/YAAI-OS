#include "apps.h"
#include "file_app.h"
void file_app_bind(const AppServices *services);
static WindowManager *windows;
static const AppDefinition *definitions[] = {&notes_application, &calculator_application,
                                             &settings_application, &browser_application};
static const AppFiles file_services = {files_read,           files_write,   files_read_id, files_write_at,
                                       files_list_directory, files_entry,   files_resolve, files_path,
                                       files_usage,          files_mkdir,   files_rename,  files_move,
                                       files_trash,          files_restore, files_remove};
static const AppServices services = {&file_services};
void apps_initialize(WindowManager *manager) { windows = manager; }
const AppDefinition *apps_find(unsigned id) {
    for (unsigned i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i)
        if (definitions[i]->id == id)
            return definitions[i];
    return 0;
}
int apps_launch(unsigned id) {
    const AppDefinition *app = apps_find(id);
    if (!windows || !app || !windows_open(windows, id))
        return 0;
    if (app->open)
        app->open(&services);
    return 1;
}
int apps_close(unsigned id) {
    const AppDefinition *app = apps_find(id);
    if (!windows || !app)
        return 0;
    if (app->close && !app->close())
        return 0;
    windows_close(windows, id);
    return 1;
}
void apps_draw(unsigned id, const BootInfo *screen, GuiRect window) {
    const AppDefinition *app = apps_find(id);
    if (app && app->draw)
        app->draw(screen, window);
}
void apps_key(unsigned id, char key) {
    const AppDefinition *app = apps_find(id);
    if (app && app->key)
        app->key(key);
}
void apps_click(unsigned id, GuiRect window, int x, int y) {
    const AppDefinition *app = apps_find(id);
    if (app && app->click)
        app->click(window, x, y);
}
void apps_pointer(unsigned id, GuiRect window, int x, int y, unsigned buttons) {
    const AppDefinition *app = apps_find(id);
    if (app && app->pointer)
        app->pointer(window, x, y, buttons);
}
int apps_escape(unsigned id) {
    const AppDefinition *app = apps_find(id);
    return app && app->escape ? app->escape() : 0;
}

static uint64_t frame_ticks;
static unsigned documents_revision;
void apps_tick(void) { ++frame_ticks; }
uint64_t apps_ticks(void) { return frame_ticks; }
int apps_active(void) { return windows ? windows->active : 0; }
void apps_documents_changed(void) { ++documents_revision; }
unsigned apps_document_revision(void) { return documents_revision; }
int apps_open_document(unsigned id) {
    if (!windows)
        return 0;
    file_app_bind(&services);
    file_app_open();
    if (file_app_document_id() == id && id)
        return apps_launch(APP_NOTES);
    if (!file_app_load(id))
        return 0;
    return apps_launch(APP_NOTES);
}
int apps_new_document(unsigned parent) {
    if (!windows)
        return 0;
    file_app_bind(&services);
    file_app_open();
    if (!file_app_new(parent))
        return 0;
    return apps_launch(APP_NOTES);
}
