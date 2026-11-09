#ifndef YAAI_POWER_H
#define YAAI_POWER_H
#include "boot_info.h"
// Returns an error message only if power control did not complete.
const char *power_finish(const BootInfo *boot, int restart);
#endif
