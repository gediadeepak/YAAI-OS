#ifndef BOOT_INFO_H
#define BOOT_INFO_H
#include <stdint.h>
#include "setup.h"

#define AUDIO_DMA_PAGES 160

typedef struct {
    volatile uint32_t *framebuffer;
    uint32_t width, height, pixels_per_scanline, pixel_format;
    void *memory_map;
    uint64_t memory_map_size, memory_descriptor_size;
    uint32_t memory_descriptor_version;
    void *audio_dma;
    uint8_t *page_bitmaps;
    uint64_t page_capacity;
    SetupConfig setup;
    uint32_t *desktop_buffer, *wallpaper_buffer;
    void *runtime_services;
} BootInfo;

_Noreturn void kernel_main(const BootInfo *boot);
#endif
