#include "platform.h"
#include "../screen.h"
// Existing renderer/console APIs remain stable during the platform migration.
void serial_write(const char *text) { platform_serial_write(text); }
void screen_wait_frame(void) { platform_wait_frame(); }
