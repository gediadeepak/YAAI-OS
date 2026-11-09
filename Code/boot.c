#include <stddef.h>
#include "boot_info.h"
#include "memory.h"

#include "uefi.h"
#include "installer.h"
#include "platform/platform.h"
#include "platform/uefi_hooks.h"
#include "display_mode.h"

_Static_assert(offsetof(BootServices, exit_boot_services) == 232, "UEFI service layout");
_Static_assert(offsetof(BootServices, locate_protocol) == 320, "UEFI service layout");
_Static_assert(offsetof(SystemTable, boot_services) == 96, "UEFI system table layout");

uint64_t efi_main(void *image_handle, SystemTable *system)
{
    static const Guid graphics_guid = {
        0x9042a9de, 0x23dc, 0x4a38,
        {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
    };
    static BootInfo boot;
    boot.runtime_services = system->runtime_services;
    #if !defined(SHRI_TEST_BOOT) && !defined(YAAI_LIVE_BOOT)
    uint64_t setup_status = installer_run(image_handle, system, &boot.setup);
    if (setup_status != 0) return setup_status;
    #endif
    BootServices *services = system->boot_services;
    GraphicsOutput *graphics = 0;
    uint64_t status = services->locate_protocol(&graphics_guid, 0, (void **)&graphics);
    if (status != 0 || !graphics || !graphics->mode || !graphics->mode->info) {
        system->console_out->output_string(system->console_out, u"Graphics unavailable.\r\n");
        return 0x8000000000000003ULL;
    }
    display_select_mode(graphics,services);
    if (!graphics->mode || !graphics->mode->info) return 0x8000000000000003ULL;
    GraphicsMode *mode = graphics->mode;
    GraphicsInfo *info = mode->info;
    // Support standard RGB and BGR framebuffers for this first kernel.
    if (info->pixel_format > 1 || !mode->framebuffer_base || !info->width ||
        !info->height || info->pixels_per_scanline < info->width ||
        (uint64_t)info->pixels_per_scanline * info->height > mode->framebuffer_size / 4) {
        system->console_out->output_string(system->console_out, u"Unsupported framebuffer.\r\n");
        return 0x8000000000000003ULL;
    }
    boot.framebuffer = (volatile uint32_t *)(uintptr_t)mode->framebuffer_base;
    boot.width = info->width;
    boot.height = info->height;
    boot.pixels_per_scanline = info->pixels_per_scanline;
    boot.pixel_format = info->pixel_format;

    // Contiguous loader-owned canvases stay reserved after firmware shutdown.
    uint64_t canvas_bytes = (uint64_t)boot.width * boot.height * 4;
    if (canvas_bytes <= 64 * 1024 * 1024) {
        if (services->allocate_pool(2, canvas_bytes, (void **)&boot.desktop_buffer) == 0) {
            if (services->allocate_pool(2, canvas_bytes, (void **)&boot.wallpaper_buffer) != 0) {
                services->free_pool(boot.desktop_buffer);
                boot.desktop_buffer = 0;
            }
        }
    }

    platform_uefi_prepare(services, &boot);

    uint64_t map_size = 0, map_key = 0, descriptor_size = 0;
    uint32_t descriptor_version = 0;
    status = services->get_memory_map(&map_size, 0, &map_key,
                                     &descriptor_size, &descriptor_version);
    if (status != 0x8000000000000005ULL || descriptor_size == 0) {
        return status == 0 ? 0x8000000000000002ULL : status;
    }
    // Reserve extra descriptors for changes caused by allocations and shutdown.
    uint64_t capacity = map_size + 64 * descriptor_size;
    status = services->allocate_pool(2, capacity, &boot.memory_map); // EfiLoaderData
    if (status != 0) {
        return status;
    }

    // Size allocator metadata before the final map. Include RAM occupied by
    // firmware and the loader, since allocations may split these descriptors.
    map_size = capacity;
    status = services->get_memory_map(&map_size, boot.memory_map, &map_key,
                                     &descriptor_size, &descriptor_version);
    if (status != 0) return status;
    if (descriptor_size < sizeof(MemoryDescriptor) || map_size % descriptor_size != 0) {
        return 0x8000000000000002ULL;
    }
    for (uint64_t offset = 0; offset < map_size; offset += descriptor_size) {
        const MemoryDescriptor *descriptor = (const void *)((const uint8_t *)boot.memory_map + offset);
        if (descriptor->type < 1 || descriptor->type > 10) continue;
        if (descriptor->physical_start % MEMORY_PAGE_SIZE != 0 ||
            descriptor->page_count > (UINT64_MAX - descriptor->physical_start) / MEMORY_PAGE_SIZE) {
            return 0x8000000000000002ULL;
        }
        uint64_t end_page = descriptor->physical_start / MEMORY_PAGE_SIZE + descriptor->page_count;
        if (end_page > boot.page_capacity) boot.page_capacity = end_page;
    }
    if (!boot.page_capacity) return 0x8000000000000009ULL;
    uint64_t bitmap_bytes = (boot.page_capacity + 7) / 8;
    status = services->allocate_pool(2, bitmap_bytes * 2, (void **)&boot.page_bitmaps);
    if (status != 0) return status;
    system->console_out->output_string(system->console_out, u"Starting YAAI OS...\r\n");

    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        map_size = capacity;
        status = services->get_memory_map(&map_size, boot.memory_map, &map_key,
                                         &descriptor_size, &descriptor_version);
        if (status != 0) {
            break;
        }
        // No firmware calls between obtaining the map and exiting boot services.
        status = services->exit_boot_services(image_handle, map_key);
        if (status == 0) {
            boot.memory_map_size = map_size;
            boot.memory_descriptor_size = descriptor_size;
            boot.memory_descriptor_version = descriptor_version;
            platform_disable_interrupts();
            kernel_main(&boot);
        }
        if (status != 0x8000000000000002ULL) {
            break;
        }
    }

    // Firmware may be partially shut down after a failed exit. Halt safely.
    platform_disable_interrupts();
    platform_halt();
}

