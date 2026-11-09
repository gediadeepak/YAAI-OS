#ifndef GUI_H
#define GUI_H
#include "boot_info.h"

typedef struct { int x, y, width, height; } GuiRect;
typedef enum { GUI_SMALL, GUI_BODY, GUI_HEADING, GUI_DISPLAY, GUI_MONO } GuiFont;

int gui_initialize(void);
uint32_t gui_blend(uint32_t background, uint32_t foreground, unsigned alpha);
void gui_round_rect(const BootInfo *screen, GuiRect rect, int radius, uint32_t fill, unsigned alpha);
void gui_shadow(const BootInfo *screen, GuiRect rect, int radius);
void gui_line(const BootInfo *screen, int x0, int y0, int x1, int y1, int width, uint32_t fill);
void gui_text(const BootInfo *screen, int x, int y, const char *text, GuiFont font, uint32_t fill);
int gui_text_width(const char *text, GuiFont font);
void gui_mono_character(const BootInfo *screen, char character, int x, int y, uint32_t fill, uint32_t background);
int gui_wallpaper(const BootInfo *screen);
#endif
