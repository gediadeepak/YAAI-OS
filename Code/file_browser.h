#ifndef YAAI_FILE_BROWSER_H
#define YAAI_FILE_BROWSER_H
#include "gui.h"
void file_browser_click_modified(GuiRect window, int x, int y, unsigned modifiers);
unsigned file_browser_directory(void);
unsigned file_browser_selected(void);
const char *file_browser_status(void);
#endif