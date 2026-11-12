#ifndef YAAI_NOTIFICATIONS_H
#define YAAI_NOTIFICATIONS_H
#include "gui.h"
void notification_post(const char *title, const char *detail);
unsigned notification_unread(void);
void notification_mark_read(void);
int notification_tick(void);
void notification_clear(void);
GuiRect notification_clear_button(GuiRect bounds);
void notification_history_draw(const BootInfo *screen, GuiRect bounds, int clear_hovered);
void notification_toast_draw(const BootInfo *screen, GuiRect bounds);
#endif
