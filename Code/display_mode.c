#include "display_mode.h"
static uint64_t score(const GraphicsInfo *info)
{
    if (!info || info->pixel_format > 1 || info->width < 640 || info->height < 480 ||
        info->width > 1920 || info->height > 1080 || info->pixels_per_scanline < info->width) return 0;
    return (uint64_t)info->width * info->height;
}
void display_select_mode(GraphicsOutput *graphics, BootServices *services)
{
    if (!graphics || !graphics->mode || !graphics->query_mode || !graphics->set_mode || !services || !services->free_pool) return;
    GraphicsMode *mode = graphics->mode;
    // Respect a larger mode explicitly supplied by firmware (for example a
    // VirtualBox resolution matching a high-resolution host display).
    if (mode->info && mode->info->pixel_format <= 1 && mode->info->width >= 1920 && mode->info->height >= 1080 &&
        (uint64_t)mode->info->width * mode->info->height <= 3840ULL * 2160) return;
    uint64_t best = score(mode->info);
    uint32_t selected = mode->mode;
    unsigned count = mode->max_mode < 1024 ? mode->max_mode : 1024;
    for (unsigned index = 0; index < count; ++index) {
        uint64_t size = 0; GraphicsInfo *info = 0;
        uint64_t status = graphics->query_mode(graphics,index,&size,&info);
        if (!status && info && size >= sizeof(*info)) {
            uint64_t candidate = score(info);
            if (candidate > best) { best = candidate; selected = index; }
        }
        if (info) services->free_pool(info);
    }
    // Keep the current usable mode if firmware rejects the selected mode.
    if (best && selected != mode->mode) {
        uint32_t previous = mode->mode;
        if (graphics->set_mode(graphics,selected)) graphics->set_mode(graphics,previous);
    }
}
