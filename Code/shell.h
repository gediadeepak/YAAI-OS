#ifndef SHELL_H
#define SHELL_H
#include "boot_info.h"
_Noreturn void shell_run(const BootInfo *boot);
void shell_begin(const BootInfo *boot);
void shell_key(char character);
#endif
