#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>
typedef struct { int x, y; uint8_t buttons; } MouseState;
void mouse_reset(int width, int height);
int mouse_feed_byte(uint8_t byte, MouseState *event);
int mouse_read(MouseState *event);
#endif
