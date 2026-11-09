#ifndef YAAI_SETTINGS_H
#define YAAI_SETTINGS_H
#include "boot_info.h"
typedef struct { unsigned wallpaper, volume, zone; } DesktopSettings;
void settings_initialize(const BootInfo *boot);
const DesktopSettings *settings_current(void);
const char *settings_status(void);
void settings_wallpaper(unsigned value);
void settings_volume(unsigned value);
void settings_zone(unsigned value);
#endif
