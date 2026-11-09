#ifndef YAAI_DISPLAY_MODE_H
#define YAAI_DISPLAY_MODE_H
#include "uefi.h"
// Select a real framebuffer mode, never scale or stretch an existing image.
void display_select_mode(GraphicsOutput *graphics, BootServices *services);
#endif
