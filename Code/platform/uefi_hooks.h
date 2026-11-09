#ifndef YAAI_UEFI_HOOKS_H
#define YAAI_UEFI_HOOKS_H
#include "../uefi.h"
#include "../boot_info.h"
void platform_uefi_prepare(BootServices *services, BootInfo *boot);
#endif
