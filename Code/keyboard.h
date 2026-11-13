#ifndef KEYBOARD_H
#define KEYBOARD_H
int keyboard_initialize(void);
// US layout. Returns zero when there is no printable key, Enter, or Backspace.
char keyboard_read(void);
unsigned keyboard_modifiers(void);
#endif
