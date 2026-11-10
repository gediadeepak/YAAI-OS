#ifndef YAAI_QUICK_SETTINGS_H
#define YAAI_QUICK_SETTINGS_H
#include "clock.h"
#include "gui.h"
typedef struct {
    DateTime now;
    int clock_ready, audio;
    const char *network, *address, *wireless;
} QuickInfo;
typedef struct {
    int year, month, focus, dragging;
    unsigned preview, restore_volume;
} QuickSettings;
enum { QUICK_CLOSE = -1, QUICK_SETTINGS = 3, QUICK_RESTART = 13, QUICK_SHUTDOWN = 14, QUICK_CONTROLS = 12 };
QuickInfo quick_settings_info(void);
void quick_settings_open(QuickSettings *state, const QuickInfo *info);
void quick_settings_cancel(QuickSettings *state);
GuiRect quick_settings_bounds(unsigned width, unsigned height);
GuiRect quick_settings_control(GuiRect bounds, unsigned control);
int quick_calendar_days(int year, int month);
int quick_calendar_first(int year, int month);
int quick_settings_hit(GuiRect bounds, int x, int y);
int quick_settings_click(QuickSettings *state, GuiRect bounds, const QuickInfo *info, int x, int y);
int quick_settings_pointer(QuickSettings *state, GuiRect bounds, const QuickInfo *info, int x, int y,
                           unsigned buttons);
int quick_settings_key(QuickSettings *state, const QuickInfo *info, char key);
void quick_settings_draw(const BootInfo *screen, const QuickSettings *state, const QuickInfo *info,
                         GuiRect bounds);
#endif