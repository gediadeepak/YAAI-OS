#ifndef INSTALL_DISK_H
#define INSTALL_DISK_H
#include "uefi.h"
#include "setup.h"

// 1 = blank, 2 = a marked incomplete YAAI installation, 0 = unavailable/in use.
int install_disk_state(BlockIO *disk);
// Formats a confirmed blank disk with GPT, FAT32 EFI and native YAAIFS data.
// Installs the original on-disk EFI executable and a checksummed setup file.
int install_disk_write(BlockIO *disk, const uint8_t *image, uint64_t image_size,
                       SetupConfig *config);
#endif
