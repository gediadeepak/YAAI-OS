#include "apps.h"
#include "file_app.h"
void file_app_bind(const AppServices *services);
static void open(const AppServices *services) {
    file_app_bind(services);
    file_app_open();
}
const AppDefinition notes_application = {
    APP_NOTES,        "Files & Notes", 544,           340, open, file_app_draw, file_app_key, file_app_click,
    file_app_pointer, file_app_escape, file_app_close};
