#ifndef INSTALLER_H
#define INSTALLER_H
#include "uefi.h"
#include "setup.h"

// Runs before ExitBootServices, using firmware disk and filesystem protocols.
uint64_t installer_run(void *image_handle, SystemTable *system, SetupConfig *config);
#endif
