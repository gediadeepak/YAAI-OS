#ifndef YAAI_BOOT_EFI_H
#define YAAI_BOOT_EFI_H
#include "boot_update.h"
#include "uefi.h"
typedef struct {
    File *root;
    BootServices *services;
} EfiBootStore;
BootStore boot_efi_store(EfiBootStore *context, File *root, BootServices *services);
uint64_t boot_efi_start(SystemTable *system, void *parent, void *device, File *root, unsigned key,
                        int skip_dispatch);
void boot_slots_start(void *image_handle, SystemTable *system);
#endif
