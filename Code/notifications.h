#ifndef YAAI_NOTIFICATIONS_H
#define YAAI_NOTIFICATIONS_H
#include "gui.h"
void notification_post(const char *title, const char *detail);
unsigned notification_unread(void);
void notification_mark_read(void);
int notification_tick(void);
void notification_history_draw(const BootInfo *screen, GuiRect bounds);
void notification_toast_draw(const BootInfo *screen, GuiRect bounds);
#endif
