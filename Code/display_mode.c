#include "display_mode.h"
#include "storage.h"
static uint64_t score(const GraphicsInfo *info) {
    if (!info || info->pixel_format > 1 || info->width < 640 || info->height < 480 || info->width > 1920 ||
        info->height > 1080 || info->pixels_per_scanline < info->width)
        return 0;
    return (uint64_t)info->width * info->height;
}
void display_select_mode(GraphicsOutput *graphics, BootServices *services) {
    if (!graphics || !graphics->mode || !graphics->query_mode || !graphics->set_mode || !services ||
        !services->free_pool)
        return;
    GraphicsMode *mode = graphics->mode;
    uint32_t saved[2];
    unsigned length = sizeof(saved);
    unsigned preference = storage_get("Display", saved, &length) == STORE_OK && length == sizeof(saved) &&
                                  saved[0] == 1 && saved[1] < 4
                              ? saved[1]
                              : 0;
    static const unsigned widths[4] = {0, 1024, 1280, 1920}, heights[4] = {0, 768, 720, 1080};
    if (preference) {
        unsigned count = mode->max_mode < 1024 ? mode->max_mode : 1024;
        for (unsigned index = 0; index < count; ++index) {
            uint64_t size = 0;
            GraphicsInfo *info = 0;
            uint64_t result = graphics->query_mode(graphics, index, &size, &info);
            int match = !result && info && size >= sizeof(*info) && score(info) &&
                        info->width == widths[preference] && info->height == heights[preference];
            if (info)
                services->free_pool(info);
            if (match) {
                uint32_t previous = mode->mode;
                if (index == previous || !graphics->set_mode(graphics, index))
                    return;
                graphics->set_mode(graphics, previous);
                break;
            }
        }
    }
    // Respect a larger mode explicitly supplied by firmware (for example a
    // VirtualBox resolution matching a high-resolution host display).
    if (mode->info && mode->info->pixel_format <= 1 && mode->info->width >= 1920 &&
        mode->info->height >= 1080 && (uint64_t)mode->info->width * mode->info->height <= 3840ULL * 2160)
        return;
    uint64_t best = score(mode->info);
    uint32_t selected = mode->mode;
    unsigned count = mode->max_mode < 1024 ? mode->max_mode : 1024;
    for (unsigned index = 0; index < count; ++index) {
        uint64_t size = 0;
        GraphicsInfo *info = 0;
        uint64_t status = graphics->query_mode(graphics, index, &size, &info);
        if (!status && info && size >= sizeof(*info)) {
            uint64_t candidate = score(info);
            if (candidate > best) {
                best = candidate;
                selected = index;
            }
        }
        if (info)
            services->free_pool(info);
    }
    // Keep the current usable mode if firmware rejects the selected mode.
    if (best && selected != mode->mode) {
        uint32_t previous = mode->mode;
        if (graphics->set_mode(graphics, selected))
            graphics->set_mode(graphics, previous);
    }
}
