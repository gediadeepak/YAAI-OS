#ifndef PS2_H
#define PS2_H
int ps2_initialize(void);
int ps2_mouse_available(void);
int ps2_keyboard_byte(void);
int ps2_mouse_byte(void);
int ps2_keyboard_pending(void);
#endif
