#ifndef VBOX_POINTER_H
#define VBOX_POINTER_H
#include "../platform.h"
void vbox_pointer_initialize(unsigned width, unsigned height);
int vbox_pointer_available(void);
int vbox_pointer_read(PlatformPointer *event);
int vbox_poweroff(void);
#endif
