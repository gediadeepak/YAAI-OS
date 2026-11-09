#include "gui.h"
#include "screen.h"

extern const uint8_t gui_fonts_start[], gui_fonts_end[];
extern const uint8_t gui_wallpaper_start[], gui_wallpaper_end[];

typedef struct {
    unsigned width, height;
    const uint8_t *advances, *alpha;
} FontAtlas;
static FontAtlas fonts[5];

static unsigned read16(const uint8_t *bytes)
{
    return bytes[0] | ((unsigned)bytes[1] << 8);
}

static uint32_t read32(const uint8_t *bytes)
{
    return bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int gui_initialize(void)
{
    const uint8_t *position = gui_fonts_start;
    if (gui_fonts_end - position < 8 || read32(position) != 0x31544e46 || read32(position + 4) != 5) return 0;
    position += 8;
    for (unsigned index = 0; index < 5; ++index) {
        if (gui_fonts_end - position < 12) return 0;
        FontAtlas *font = &fonts[index];
        font->width = read16(position); font->height = read16(position + 2);
        if (!font->width || font->width > 128 || !font->height || font->height > 128 ||
            read16(position + 4) != 32 || read16(position + 6) != 95) return 0;
        position += 12;
        uint64_t bytes = 95 + (uint64_t)font->width * font->height * 95;
        if ((uint64_t)(gui_fonts_end - position) < bytes) return 0;
        font->advances = position;
        font->alpha = position + 95;
        position += bytes;
    }
    return position == gui_fonts_end;
}

uint32_t gui_blend(uint32_t background, uint32_t foreground, unsigned alpha)
{
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned value = (((background >> shift) & 255) * (255 - alpha) +
                          ((foreground >> shift) & 255) * alpha + 127) / 255;
        result |= value << shift;
    }
    return result;
}

static void blend_pixel(const BootInfo *screen, int x, int y, uint32_t fill, unsigned alpha)
{
    if (x < 0 || y < 0 || (uint32_t)x >= screen->width || (uint32_t)y >= screen->height || !alpha) return;
    volatile uint32_t *destination = screen->framebuffer + (uint64_t)y * screen->pixels_per_scanline + x;
    *destination = alpha == 255 ? fill : gui_blend(*destination, fill, alpha);
}

static int rounded_sample(int x, int y, int width, int height, int radius)
{
    int center_x = x < radius ? radius : (x > width - radius ? width - radius : x);
    int center_y = y < radius ? radius : (y > height - radius ? height - radius : y);
    int dx = x - center_x, dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

void gui_round_rect(const BootInfo *screen, GuiRect rect, int radius, uint32_t fill, unsigned alpha)
{
    if (rect.width <= 0 || rect.height <= 0 || !alpha) return;
    if (radius > rect.width / 2) radius = rect.width / 2;
    if (radius > rect.height / 2) radius = rect.height / 2;
    int left = rect.x < 0 ? 0 : rect.x;
    int top = rect.y < 0 ? 0 : rect.y;
    int right = rect.x + rect.width > (int)screen->width ? (int)screen->width : rect.x + rect.width;
    int bottom = rect.y + rect.height > (int)screen->height ? (int)screen->height : rect.y + rect.height;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            unsigned coverage = 4;
            int local_x = x - rect.x, local_y = y - rect.y;
            if ((local_x < radius || local_x >= rect.width - radius) &&
                (local_y < radius || local_y >= rect.height - radius)) {
                coverage = 0;
                for (int sy = 1; sy <= 3; sy += 2)
                    for (int sx = 1; sx <= 3; sx += 2)
                        coverage += rounded_sample(local_x * 4 + sx, local_y * 4 + sy,
                                                   rect.width * 4, rect.height * 4, radius * 4);
            }
            blend_pixel(screen,x,y,fill,alpha * coverage / 4);
        }
    }
}

void gui_shadow(const BootInfo *screen, GuiRect rect, int radius)
{
    int inner = radius * radius, outer = (radius + 20) * (radius + 20);
    for (int y = rect.y - 12; y < rect.y + rect.height + 28; ++y) {
        for (int x = rect.x - 20; x < rect.x + rect.width + 20; ++x) {
            if (x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height) continue;
            int local_x = x - rect.x, local_y = y - rect.y - 8;
            int cx = local_x < radius ? radius : (local_x > rect.width - radius ? rect.width - radius : local_x);
            int cy = local_y < radius ? radius : (local_y > rect.height - radius ? rect.height - radius : local_y);
            int dx = local_x - cx, dy = local_y - cy;
            int distance = dx * dx + dy * dy;
            if (distance < outer) {
                unsigned alpha = distance <= inner ? 64 : (unsigned)(outer - distance) * 64 / (outer - inner);
                blend_pixel(screen,x,y,color(screen,0,3,9),alpha);
            }
        }
    }
}

void gui_line(const BootInfo *screen, int x0, int y0, int x1, int y1, int width, uint32_t fill)
{
    int left = (x0 < x1 ? x0 : x1) - width;
    int top = (y0 < y1 ? y0 : y1) - width;
    int right = (x0 > x1 ? x0 : x1) + width;
    int bottom = (y0 > y1 ? y0 : y1) + width;
    int64_t dx = (x1 - x0) * 4, dy = (y1 - y0) * 4;
    int64_t length = dx * dx + dy * dy;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            unsigned coverage = 0;
            for (int sy = 1; sy <= 3; sy += 2) {
                for (int sx = 1; sx <= 3; sx += 2) {
                    int64_t px = (x - x0) * 4 + sx, py = (y - y0) * 4 + sy;
                    int64_t projection = px * dx + py * dy;
                    if (projection < 0) projection = 0;
                    if (projection > length) projection = length;
                    int64_t ex = px - (length ? dx * projection / length : 0);
                    int64_t ey = py - (length ? dy * projection / length : 0);
                    coverage += ex * ex + ey * ey <= (int64_t)width * width * 4;
                }
            }
            blend_pixel(screen,x,y,fill,coverage * 255 / 4);
        }
    }
}

static unsigned glyph_index(char character)
{
    return character >= 32 && character <= 126 ? (unsigned)(character - 32) : '?' - 32;
}

static void glyph(const BootInfo *screen, int x, int y, char character, GuiFont id, uint32_t fill)
{
    const FontAtlas *font = &fonts[id];
    const uint8_t *alpha = font->alpha + (uint64_t)glyph_index(character) * font->width * font->height;
    for (unsigned row = 0; row < font->height; ++row) {
        for (unsigned column = 0; column < font->width; ++column) {
            unsigned coverage = alpha[row * font->width + column];
            if (coverage) blend_pixel(screen,x + column,y + row,fill,coverage);
        }
    }
}

int gui_text_width(const char *text, GuiFont id)
{
    int width = 0;
    for (; *text; ++text) width += fonts[id].advances[glyph_index(*text)];
    return width;
}

void gui_text(const BootInfo *screen, int x, int y, const char *text, GuiFont id, uint32_t fill)
{
    for (; *text; ++text) {
        glyph(screen,x,y,*text,id,fill);
        x += fonts[id].advances[glyph_index(*text)];
    }
}

void gui_mono_character(const BootInfo *screen, char character, int x, int y, uint32_t fill, uint32_t background)
{
    gui_round_rect(screen,(GuiRect){x,y,12,24},0,background,255);
    glyph(screen,x + 1,y,character,GUI_MONO,fill);
}

int gui_wallpaper(const BootInfo *screen)
{
    if (gui_wallpaper_end - gui_wallpaper_start < 8) return 0;
    unsigned width = read32(gui_wallpaper_start), height = read32(gui_wallpaper_start + 4);
    if (!width || !height || width > 8192 || height > 8192 ||
        (uint64_t)(gui_wallpaper_end - gui_wallpaper_start) != 8 + (uint64_t)width * height * 3) return 0;
    const uint8_t *pixels = gui_wallpaper_start + 8;
    uint64_t source_width = width, source_height = height;
    if ((uint64_t)screen->width * height < (uint64_t)screen->height * width)
        source_width = (uint64_t)screen->width * height / screen->height;
    else source_height = (uint64_t)screen->height * width / screen->width;
    unsigned start_x = (unsigned)(width - source_width) / 2, start_y = (unsigned)(height - source_height) / 2;
    for (unsigned y = 0; y < screen->height; ++y) {
        unsigned source_y = start_y + (unsigned)((uint64_t)y * source_height / screen->height);
        for (unsigned x = 0; x < screen->width; ++x) {
            unsigned source_x = start_x + (unsigned)((uint64_t)x * source_width / screen->width);
            const uint8_t *sample = pixels + ((uint64_t)source_y * width + source_x) * 3;
            screen->framebuffer[(uint64_t)y * screen->pixels_per_scanline + x] = color(screen,sample[0],sample[1],sample[2]);
        }
    }
    return 1;
}
