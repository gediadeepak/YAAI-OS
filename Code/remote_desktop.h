#ifndef YAAI_REMOTE_DESKTOP_H
#define YAAI_REMOTE_DESKTOP_H
#include "boot_info.h"

int remote_desktop_available(void);
int remote_desktop_enabled(void);
void remote_desktop_toggle(void);
void remote_desktop_poll(const BootInfo *display);

#endif
