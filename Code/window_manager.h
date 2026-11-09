#ifndef YAAI_WINDOW_MANAGER_H
#define YAAI_WINDOW_MANAGER_H
#include "gui.h"
enum { WINDOW_LIMIT = 8 };
enum { WINDOW_NORMAL, WINDOW_MAXIMIZED, WINDOW_LEFT, WINDOW_RIGHT };
typedef struct {
    GuiRect rect[WINDOW_LIMIT], restore[WINDOW_LIMIT];
    int visible[WINDOW_LIMIT], opened[WINDOW_LIMIT], layout[WINDOW_LIMIT];
    int order[WINDOW_LIMIT - 1], active, cycle[WINDOW_LIMIT - 1];
    unsigned count, cycle_count, cycle_index;
} WindowManager;
void windows_initialize(WindowManager *manager);
int windows_register(WindowManager *manager, unsigned id, GuiRect rect);
int windows_focus(WindowManager *manager, unsigned id);
int windows_open(WindowManager *manager, unsigned id);
void windows_hide(WindowManager *manager, unsigned id);
void windows_close(WindowManager *manager, unsigned id);
int windows_cycle(WindowManager *manager, int reverse);
void windows_cycle_end(WindowManager *manager);
int windows_arrange(WindowManager *manager, unsigned id, int layout, GuiRect area, int min_width,
                    int min_height);
#endif
