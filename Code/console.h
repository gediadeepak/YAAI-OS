#ifndef CONSOLE_H
#define CONSOLE_H
#include "boot_info.h"

void console_initialize(const BootInfo *boot);
void console_clear(void);
void console_write(const char *text);
void console_backspace(void);
void console_cursor(int visible);
void console_hex(uint64_t value);
void console_decimal(uint64_t value);
unsigned console_columns(void);
void console_set_viewport(const BootInfo *boot, int left, int top, int width, int height);
void console_redraw(void);
void console_reframe(int x, int y, int width, int height);
void console_restore_fullscreen(void);
#endif
