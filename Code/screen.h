#ifndef SCREEN_H
#define SCREEN_H
#include "boot_info.h"

uint32_t color(const BootInfo *boot, uint8_t red, uint8_t green, uint8_t blue);
void pixel(const BootInfo *boot, int x, int y, uint32_t value);
void screen_character(const BootInfo *boot, char character, int left, int top,
                      int scale, uint32_t foreground, uint32_t background);
void serial_write(const char *text);
void screen_character_transparent(const BootInfo *boot, char character, int left,
                                  int top, int scale, uint32_t foreground);
void screen_wait_frame(void);
#endif
