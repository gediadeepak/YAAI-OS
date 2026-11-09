#ifndef AUDIO_H
#define AUDIO_H
#include "boot_info.h"

// Returns nonzero when the QEMU AC97 device starts playing the boot chime.
int audio_start(const BootInfo *boot);
int audio_available(void);
void audio_volume(unsigned percent);
#endif
