#ifndef YAAI_WINDOW_MANAGER_H
#define YAAI_WINDOW_MANAGER_H
#include "gui.h"
enum { WINDOW_LIMIT = 8 };
typedef struct {
    GuiRect rect[WINDOW_LIMIT];
    int visible[WINDOW_LIMIT], order[WINDOW_LIMIT - 1], active;
    unsigned count;
} WindowManager;
void windows_initialize(WindowManager *manager);
int windows_register(WindowManager *manager, unsigned id, GuiRect rect);
int windows_focus(WindowManager *manager, unsigned id);
int windows_open(WindowManager *manager, unsigned id);
void windows_hide(WindowManager *manager, unsigned id);
#endif
