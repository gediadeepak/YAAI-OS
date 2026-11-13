#ifndef YAAI_INPUT_EVENTS_H
#define YAAI_INPUT_EVENTS_H
#include "platform/platform.h"
// Portable event decoder. Wire event codes follow the Virtio input convention.
void input_events_initialize(unsigned width, unsigned height);
void input_events_absolute_range(unsigned axis, int32_t minimum, int32_t maximum);
void input_events_feed(uint16_t type, uint16_t code, int32_t value);
int input_events_key_pending(void);
char input_events_key_read(void);
unsigned input_events_modifiers(void);
int input_events_pointer_read(PlatformPointer *event);
#endif
