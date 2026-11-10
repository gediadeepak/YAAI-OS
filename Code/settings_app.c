#include "settings_app.h"
#include "apps.h"
#include "brand.h"
#include "edit_keys.h"
#include "memory.h"
#include "platform/platform.h"
#include "screen.h"
#include "settings.h"

enum { HOME = 6, SEARCH_LIMIT = 48, HIT_LIMIT = 48 };
static unsigned page = HOME;
static int scroll, offset, content_height, searching, drag_scroll, query_selected;
static char query[SEARCH_LIMIT];
static const BootInfo *screen, *device;
static BootInfo viewport;
static GuiRect view, search_box, scroll_track;
typedef struct {
    GuiRect rect;
    int action;
} Hit;
static Hit hits[HIT_LIMIT];
static unsigned hit_count;
static const char *names[] = {"Display", "Personalization", "Sound", "Time & language",
                              "About",   "Devices",         "Home"};
static const char *descriptions[] = {
    "Resolution, display size and scaling", "Wallpaper, colors and accent themes",
    "Volume, mute and test sound",          "Country, time zone and local time",
    "OS version, memory and storage",       "Network, wireless and power status"};
static const char *keywords[] = {"display screen resolution scaling size monitor",
                                 "personalization appearance wallpaper background theme color colour accent",
                                 "sound audio speaker volume mute",
                                 "time language country zone date clock",
                                 "about system version memory ram storage information",
                                 "devices network ethernet wifi wireless battery power sim bluetooth"};
unsigned settings_app_page(void) { return page; }
int settings_app_scroll(void) { return scroll; }
void settings_app_select(unsigned p) {
    if (p <= HOME) {
        page = p;
        scroll = 0;
        query[0] = 0;
        searching = drag_scroll = query_selected = 0;
    }
}
static int length(const char *s) {
    int n = 0;
    while (s[n])
        ++n;
    return n;
}
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int contains(const char *s, const char *needle) {
    if (!needle[0])
        return 1;
    for (unsigned i = 0; s[i]; ++i) {
        unsigned k = 0;
        while (needle[k] && s[i + k] && lower(s[i + k]) == lower(needle[k]))
            ++k;
        if (!needle[k])
            return 1;
    }
    return 0;
}
static int inside(GuiRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}

static uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return color(screen, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static uint32_t accent(void) { return settings_current()->theme ? rgb(164, 117, 42) : rgb(0, 116, 135); }
static uint32_t ink(void) { return rgb(28, 38, 53); }
static uint32_t muted(void) { return rgb(98, 112, 131); }
static void fill(GuiRect r, int radius, uint32_t c) {
    r.y -= offset;
    gui_round_rect(screen, r, radius, c, 255);
}
static void line(int x, int y, int x2, int y2, int width, uint32_t c) {
    gui_line(screen, x, y - offset, x2, y2 - offset, width, c);
}
static void text(int x, int y, const char *s, GuiFont f, uint32_t c) {
    gui_text(screen, x, y - offset, s, f, c);
}
static void clipped(int x, int y, int width, const char *s, GuiFont f, uint32_t c) {
    char out[192];
    unsigned i = 0;
    for (; s[i] && i + 1 < sizeof(out); ++i) {
        out[i] = s[i];
        out[i + 1] = 0;
        if (gui_text_width(out, f) > width) {
            out[i] = 0;
            break;
        }
    }
    out[i] = 0;
    text(x, y, out, f, c);
}
static void number_text(char *out, uint64_t n, const char *suffix) {
    char rev[24];
    unsigned count = 0, at = 0;
    do {
        rev[count++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (count)
        out[at++] = rev[--count];
    for (unsigned i = 0; suffix[i] && at < 95; ++i)
        out[at++] = suffix[i];
    out[at] = 0;
}
static void value(int x, int y, uint64_t n, const char *suffix) {
    char s[96];
    number_text(s, n, suffix);
    text(x, y, s, GUI_SMALL, muted());
}
static void hit(GuiRect r, int action) {
    if (hit_count < HIT_LIMIT)
        hits[hit_count++] = (Hit){r, action};
}
static void icon(int kind, int x, int y, uint32_t c) {
    if (kind == 0) {
        fill((GuiRect){x, y + 2, 21, 14}, 3, c);
        fill((GuiRect){x + 2, y + 4, 17, 10}, 1, rgb(244, 248, 251));
        line(x + 10, y + 16, x + 10, y + 21, 2, c);
        line(x + 5, y + 21, x + 15, y + 21, 2, c);
    } else if (kind == 1) {
        fill((GuiRect){x, y + 2, 21, 18}, 4, c);
        fill((GuiRect){x + 3, y + 5, 15, 12}, 2, rgb(242, 248, 250));
        line(x + 5, y + 15, x + 10, y + 9, 2, c);
        line(x + 10, y + 9, x + 17, y + 15, 2, c);
    } else if (kind == 2) {
        line(x + 3, y + 9, x + 8, y + 9, 5, c);
        line(x + 8, y + 7, x + 12, y + 4, 4, c);
        line(x + 12, y + 4, x + 12, y + 19, 3, c);
        line(x + 16, y + 7, x + 20, y + 11, 2, c);
        line(x + 20, y + 11, x + 16, y + 16, 2, c);
    } else if (kind == 3) {
        fill((GuiRect){x, y, 22, 22}, 11, c);
        fill((GuiRect){x + 2, y + 2, 18, 18}, 9, rgb(248, 250, 252));
        line(x + 11, y + 5, x + 11, y + 11, 2, c);
        line(x + 11, y + 11, x + 16, y + 14, 2, c);
    } else if (kind == 4) {
        fill((GuiRect){x, y, 22, 22}, 11, c);
        text(x + 8, y + 3, "i", GUI_SMALL, rgb(255, 255, 255));
    } else if (kind == 5) {
        fill((GuiRect){x, y + 1, 21, 14}, 3, c);
        fill((GuiRect){x + 3, y + 4, 15, 8}, 1, rgb(246, 249, 252));
        fill((GuiRect){x + 7, y + 17, 9, 5}, 2, c);
    } else {
        line(x + 2, y + 10, x + 11, y + 2, 2, c);
        line(x + 11, y + 2, x + 21, y + 10, 2, c);
        line(x + 5, y + 9, x + 5, y + 21, 2, c);
        line(x + 5, y + 21, x + 17, y + 21, 2, c);
        line(x + 17, y + 21, x + 17, y + 9, 2, c);
    }
}
static void card(GuiRect r) {
    fill(r, 12, rgb(218, 225, 233));
    fill((GuiRect){r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 11, rgb(255, 255, 255));
}
static void row(GuiRect r, int symbol, const char *title, const char *subtitle, int action) {
    icon(symbol, r.x + 16, r.y + 17, accent());
    clipped(r.x + 52, r.y + 10, r.width - 78, title, GUI_SMALL, ink());
    if (subtitle && subtitle[0])
        clipped(r.x + 52, r.y + 31, r.width - 78, subtitle, GUI_SMALL, muted());
    if (action >= 0) {
        text(r.x + r.width - 22, r.y + 22, ">", GUI_SMALL, muted());
        hit(r, action);
    }
}
static uint32_t preview_pixels[160 * 90];
static int preview_ready;
static void thumbnail(GuiRect r, unsigned style) {
    if (!preview_ready) {
        BootInfo p = *device;
        p.framebuffer = preview_pixels;
        p.width = 160;
        p.height = 90;
        p.pixels_per_scanline = 160;
        preview_ready = gui_wallpaper(&p);
    }
    fill(r, 8, style == 2 ? rgb(13, 68, 73) : rgb(18, 32, 57));
    int target_width = r.width - 6, target_height = r.height - 6;
    if (target_width <= 0 || target_height <= 0)
        return;
    int source_width = 160, source_height = 90;
    if (target_width * 90 > target_height * 160)
        source_height = 160 * target_height / target_width;
    else
        source_width = 90 * target_width / target_height;
    if (source_width < 1)
        source_width = 1;
    if (source_height < 1)
        source_height = 1;
    for (int y = 3; y < r.height - 3; ++y)
        for (int x = 3; x < r.width - 3; ++x) {
            uint32_t c;
            if (style == 0 && preview_ready) {
                unsigned sx = (unsigned)((160 - source_width) / 2 + (x - 3) * source_width / target_width);
                unsigned sy = (unsigned)((90 - source_height) / 2 + (y - 3) * source_height / target_height);
                c = preview_pixels[sy * 160 + sx];
            } else {
                unsigned glow = (unsigned)(x + y) * 30 / (unsigned)(r.width + r.height);
                c = style == 2 ? rgb(5, 28 + glow, 38 + glow / 2) : rgb(8, 14 + glow / 2, 29 + glow);
            }
            pixel(screen, r.x + x, r.y + y - offset, c);
        }
    if (settings_current()->wallpaper == style) {
        line(r.x + 6, r.y + r.height - 3, r.x + r.width - 6, r.y + r.height - 3, 3, accent());
    }
}
static void layout(GuiRect w) {
    int sidebar = w.width < 850 ? 178 : 216;
    view = (GuiRect){w.x + sidebar + 24, w.y + 128, w.width - sidebar - 48, w.height - 174};
    search_box = (GuiRect){view.x, w.y + 67, view.width, 38};
    scroll_track = (GuiRect){w.x + w.width - 16, view.y, 6, view.height};
}
static int maximum_scroll(void) {
    int m = content_height - view.height;
    return m > 0 ? m : 0;
}
static void clamp_scroll(void) {
    if (scroll < 0)
        scroll = 0;
    if (scroll > maximum_scroll())
        scroll = maximum_scroll();
}
static void time_text(char out[20]) {
    DateTime t;
    if (!clock_local_time(settings_current()->zone, &t)) {
        out[0] = 0;
        return;
    }
    const char *base = "0000-00-00 00:00:00";
    for (unsigned i = 0; i < 20; ++i)
        out[i] = base[i];
    unsigned fields[6] = {(unsigned)t.year % 100, (unsigned)t.month,  (unsigned)t.day,
                          (unsigned)t.hour,       (unsigned)t.minute, (unsigned)t.second};
    unsigned positions[6] = {2, 5, 8, 11, 14, 17};
    out[0] = (char)('0' + (unsigned)t.year / 1000 % 10);
    out[1] = (char)('0' + (unsigned)t.year / 100 % 10);
    for (unsigned i = 0; i < 6; ++i) {
        out[positions[i]] = (char)('0' + fields[i] / 10);
        out[positions[i] + 1] = (char)('0' + fields[i] % 10);
    }
}
static void home(void) {
    int width = (int)screen->width, two = width >= 650, gap = 16, cw = two ? (width - gap) / 2 : width;
    text(0, 0, "Home", GUI_HEADING, ink());
    card((GuiRect){0, 48, width, 94});
    thumbnail((GuiRect){16, 62, 100, 62}, settings_current()->wallpaper);
    text(134, 66, "YAAI device", GUI_BODY, ink());
    clipped(134, 96, width - 150, platform_name(), GUI_SMALL, muted());
    int recommended_y = 164, devices_y = two ? 164 : 404, appearance_y = two ? 404 : 644,
        time_y = two ? 404 : 858;
    int right = two ? cw + gap : 0;
    GuiRect recommended = {0, recommended_y, cw, 224}, connections = {right, devices_y, cw, 224};
    card(recommended);
    text(18, recommended_y + 18, "Recommended settings", GUI_BODY, ink());
    clipped(18, recommended_y + 47, cw - 36, "Make this device feel like yours", GUI_SMALL, muted());
    row((GuiRect){2, recommended_y + 70, cw - 4, 49}, 0, "Display", "Resolution and screen size", 0);
    row((GuiRect){2, recommended_y + 120, cw - 4, 49}, 2, "Sound", "Volume and audio output", 2);
    row((GuiRect){2, recommended_y + 170, cw - 4, 49}, 4, "About this device",
        "Memory, storage and OS version", 4);
    card(connections);
    text(right + 18, devices_y + 18, "Device connections", GUI_BODY, ink());
    clipped(right + 18, devices_y + 47, cw - 36, "Status reported by this platform", GUI_SMALL, muted());
    row((GuiRect){right + 2, devices_y + 70, cw - 4, 49}, 5, "Network", platform_network_status(), 5);
    row((GuiRect){right + 2, devices_y + 120, cw - 4, 49}, 2, "Audio output",
        platform_audio_available() ? "Stereo output available" : "No supported device detected", 2);
    PlatformPower power = platform_power();
    row((GuiRect){right + 2, devices_y + 170, cw - 4, 49}, 5, "Power",
        !power.supported ? "Reporting unavailable"
        : !power.present ? "No battery reported"
        : power.charging ? "Battery charging"
                         : "Battery present",
        5);
    card((GuiRect){0, appearance_y, cw, 198});
    text(18, appearance_y + 18, "Personalize your device", GUI_BODY, ink());
    int tw = (cw - 60) / 3;
    for (unsigned i = 0; i < 3; ++i) {
        GuiRect r = {18 + (int)i * (tw + 12), appearance_y + 58, tw, 72};
        thumbnail(r, i);
        hit(r, 100 + (int)i);
    }
    row((GuiRect){2, appearance_y + 143, cw - 4, 49}, 1, "Wallpaper & colors",
        "Choose a background and accent", 1);
    card((GuiRect){right, time_y, cw, 198});
    text(right + 18, time_y + 18, "Time & language", GUI_BODY, ink());
    char stamp[20];
    time_text(stamp);
    text(right + 18, time_y + 61, stamp, GUI_BODY, ink());
    clipped(right + 18, time_y + 96, cw - 36, setup_zone_name(settings_current()->zone), GUI_SMALL, muted());
    row((GuiRect){right + 2, time_y + 143, cw - 4, 49}, 3, "Country and time zone", "Adjust your local time",
        3);
    content_height = two ? 620 : 1074;
}
static void choice(GuiRect r, const char *label, const char *subtitle, int active, int action) {
    card(r);
    if (active)
        fill((GuiRect){r.x + 1, r.y + 8, 3, r.height - 16}, 1, accent());
    clipped(r.x + 18, r.y + 13, r.width - 60, label, GUI_SMALL, ink());
    if (subtitle && subtitle[0])
        clipped(r.x + 18, r.y + 37, r.width - 40, subtitle, GUI_SMALL, muted());
    fill((GuiRect){r.x + r.width - 34, r.y + 17, 16, 16}, 8, active ? accent() : rgb(206, 215, 225));
    if (active)
        fill((GuiRect){r.x + r.width - 29, r.y + 22, 6, 6}, 3, rgb(255, 255, 255));
    hit(r, action);
}
static void details(void) {
    int width = (int)screen->width;
    const DesktopSettings *s = settings_current();
    text(0, 0, names[page], GUI_HEADING, ink());
    clipped(0, 40, width, descriptions[page], GUI_SMALL, muted());
    int y = 78;
    if (page == 0) {
        char label[96];
        number_text(label, device->width, " pixels wide");
        card((GuiRect){0, y, width, 90});
        icon(0, 18, y + 25, accent());
        text(56, y + 16, "Current display", GUI_BODY, ink());
        text(56, y + 48, label, GUI_SMALL, muted());
        value(width / 2, y + 48, device->height, " pixels high");
        y += 106;
        const char *res[] = {"Auto (recommended)", "1024 x 768", "1280 x 720", "1920 x 1080"};
        for (unsigned i = 0; i < 4; ++i) {
            choice((GuiRect){0, y, width, 65}, res[i], "Applies after restart; firmware must support it",
                   s->resolution == i, 200 + (int)i);
            y += 75;
        }
        text(0, y + 5, "UI scaling: 100% (fixed)", GUI_SMALL, muted());
        text(0, y + 32, "Unavailable resolutions fall back to Auto.", GUI_SMALL, muted());
        y += 66;
    } else if (page == 1) {
        int tw = (width - 72) / 3, th = tw * 9 / 16;
        if (th > 150)
            th = 150;
        card((GuiRect){0, y, width, th + 89});
        text(18, y + 16, "Choose your wallpaper", GUI_BODY, ink());
        const char *labels[] = {"AI human", "Midnight", "Teal"};
        for (unsigned i = 0; i < 3; ++i) {
            GuiRect r = {18 + (int)i * (tw + 18), y + 50, tw, th};
            thumbnail(r, i);
            hit(r, 100 + (int)i);
            clipped(r.x, y + th + 59, tw, labels[i], GUI_SMALL, muted());
        }
        y += th + 109;
        choice((GuiRect){0, y, width, 70}, "Ocean accent", "A calm teal highlight", !s->theme, 203);
        y += 80;
        choice((GuiRect){0, y, width, 70}, "Amber accent", "Warm golden highlights", s->theme == 1, 204);
        y += 90;
        text(0, y, "Changes are applied and saved automatically.", GUI_SMALL, muted());
        y += 40;
    } else if (page == 2) {
        card((GuiRect){0, y, width, 94});
        icon(2, 18, y + 20, accent());
        text(58, y + 16, "Output volume", GUI_BODY, ink());
        value(58, y + 52, s->volume, "%");
        clipped(112, y + 52, width - 130,
                platform_audio_available() ? "Stereo output available" : "No supported output detected",
                GUI_SMALL, muted());
        y += 110;
        const char *labels[] = {"Lower volume", "Raise volume", s->volume ? "Mute" : "Unmute",
                                "Play test sound"};
        const char *sub[] = {"Decrease by 10%", "Increase by 10%", "Save the output volume",
                             "Uses the supported sound device"};
        for (unsigned i = 0; i < 4; ++i) {
            choice((GuiRect){0, y, width, 65}, labels[i], sub[i], i == 2 && !s->volume, 200 + (int)i);
            y += 75;
        }
    } else if (page == 3) {
        char stamp[20];
        time_text(stamp);
        card((GuiRect){0, y, width, 98});
        text(18, y + 18, stamp, GUI_BODY, ink());
        clipped(18, y + 55, width - 36, "Hardware clock uses UTC; country rules handle daylight saving.",
                GUI_SMALL, muted());
        y += 116;
        for (unsigned i = 0; i < 7; ++i) {
            choice((GuiRect){0, y, width, 61}, setup_zone_name(i), "Country / time zone", s->zone == i,
                   200 + (int)i);
            y += 71;
        }
    } else if (page == 4) {
        card((GuiRect){0, y, width, 128});
        text(18, y + 18, OS_NAME, GUI_HEADING, ink());
        clipped(18, y + 62, width - 36, OS_TAGLINE, GUI_SMALL, muted());
        text(18, y + 94, "VERSION I", GUI_SMALL, accent());
        y += 146;
        card((GuiRect){0, y, width, 188});
        text(18, y + 18, "Device specifications", GUI_BODY, ink());
        MemoryStats m = memory_stats();
        clipped(18, y + 58, width - 36, platform_name(), GUI_SMALL, muted());
        value(18, y + 91, (m.usable_pages + m.retained_pages + m.limited_pages) / 256, " MiB total RAM");
        value(18, y + 121, m.free_pages / 256, " MiB available RAM");
        if (setup_valid(&device->setup))
            value(18, y + 151, device->setup.data_sectors / 2048, " MiB data storage");
        else
            text(18, y + 151, "Live session / storage not installed", GUI_SMALL, muted());
        y += 208;
        FileUsage usage;
        card((GuiRect){0, y, width, 86});
        text(18, y + 15, "Documents", GUI_BODY, ink());
        if (files_usage(&usage) == FILE_OK)
            value(18, y + 48, usage.used_bytes, " document bytes stored");
        else
            text(18, y + 48, "No documents volume mounted", GUI_SMALL, muted());
        y += 106;
    } else {
        card((GuiRect){0, y, width, 168});
        text(18, y + 18, "Network & internet", GUI_BODY, ink());
        clipped(18, y + 57, width - 36, platform_network_status(), GUI_SMALL, muted());
        text(18, y + 88, platform_network_address(), GUI_SMALL, muted());
        clipped(18, y + 119, width - 36, platform_wireless_status(), GUI_SMALL, muted());
        y += 186;
        card((GuiRect){0, y, width, 128});
        text(18, y + 18, "Bluetooth & cellular", GUI_BODY, ink());
        text(18, y + 57, "Bluetooth pairing: not implemented", GUI_SMALL, muted());
        text(18, y + 89, "SIM / modem: not implemented", GUI_SMALL, muted());
        y += 146;
        card((GuiRect){0, y, width, 128});
        text(18, y + 18, "Power & battery", GUI_BODY, ink());
        PlatformPower p = platform_power();
        text(18, y + 57,
             !p.supported ? "Battery reporting unavailable"
             : !p.present ? "No battery reported"
             : p.charging ? "Battery charging"
                          : "Battery present",
             GUI_SMALL, muted());
        if (p.supported && p.present && p.percent >= 0)
            value(18, y + 89, (unsigned)p.percent, "% battery");
        y += 148;
    }
    content_height = y + 14;
}
static void results(void) {
    int width = (int)screen->width, y = 62;
    unsigned count = 0;
    text(0, 0, "Search results", GUI_HEADING, ink());
    for (unsigned i = 0; i < 6; ++i)
        if (contains(keywords[i], query) || contains(names[i], query)) {
            card((GuiRect){0, y, width, 72});
            row((GuiRect){0, y, width, 72}, (int)i, names[i], descriptions[i], (int)i);
            y += 84;
            ++count;
        }
    if (!count) {
        text(0, 74, "No matching settings", GUI_BODY, ink());
        text(0, 111, "Try display, wallpaper, sound or time.", GUI_SMALL, muted());
        y = 150;
    }
    content_height = y + 12;
}
static void draw(const BootInfo *boot, GuiRect w) {
    device = boot;
    screen = boot;
    offset = 0;
    layout(w);
    hit_count = 0;
    int radius =
        w.x == 0 && w.y == 0 && w.width == (int)boot->width && w.height == (int)boot->height ? 0 : 16;
    fill((GuiRect){w.x + 1, w.y + 53, w.width - 2, w.height - 54}, radius, rgb(244, 246, 250));
    fill((GuiRect){w.x + 1, w.y + 53, view.x - w.x - 12, w.height - 54}, radius, rgb(239, 243, 248));
    fill((GuiRect){w.x + 20, w.y + 72, 44, 44}, 22, accent());
    text(w.x + 33, w.y + 81, "Y", GUI_BODY, rgb(255, 255, 255));
    text(w.x + 76, w.y + 76, "YAAI OS", GUI_BODY, ink());
    text(w.x + 76, w.y + 101, "Local device", GUI_SMALL, muted());
    unsigned order[7] = {HOME, 0, 1, 2, 3, 5, 4};
    for (unsigned i = 0; i < 7; ++i) {
        unsigned p = order[i];
        GuiRect r = {w.x + 12, w.y + 146 + (int)i * 38, view.x - w.x - 36, 34};
        if (p == page) {
            fill(r, 7, rgb(222, 235, 241));
            fill((GuiRect){r.x + 1, r.y + 8, 3, 18}, 1, accent());
        }
        icon((int)p, r.x + 12, r.y + 5, p == page ? accent() : muted());
        clipped(r.x + 43, r.y + 10, r.width - 50, names[p], GUI_SMALL, ink());
    }
    fill(search_box, 7, rgb(255, 255, 255));
    line(search_box.x, search_box.y + 37, search_box.x + search_box.width, search_box.y + 37,
         searching ? 2 : 1, searching ? accent() : rgb(177, 189, 204));
    int query_width = gui_text_width(query, GUI_SMALL);
    if (query_width > search_box.width - 48)
        query_width = search_box.width - 48;
    if (searching && query_selected && query[0])
        fill((GuiRect){search_box.x + 12, search_box.y + 8, query_width + 6, 23}, 3, rgb(215, 235, 242));
    if (searching && !query_selected)
        line(search_box.x + 14 + query_width, search_box.y + 9, search_box.x + 14 + query_width,
             search_box.y + 28, 1, accent());
    clipped(search_box.x + 14, search_box.y + 11, search_box.width - 45, query[0] ? query : "Find a setting",
            GUI_SMALL, query[0] ? ink() : muted());
    if (query[0])
        text(search_box.x + search_box.width - 22, search_box.y + 11, "x", GUI_SMALL, muted());
    else
        text(search_box.x + search_box.width - 24, search_box.y + 10, "/", GUI_SMALL, muted());
    if (view.width < 1 || view.height < 1)
        return;
    viewport = *boot;
    viewport.framebuffer = boot->framebuffer + (uint64_t)view.y * boot->pixels_per_scanline + view.x;
    viewport.width = (uint32_t)view.width;
    viewport.height = (uint32_t)view.height;
    screen = &viewport;
    offset = scroll;
    if (query[0])
        results();
    else if (page == HOME)
        home();
    else
        details();
    int old = scroll;
    clamp_scroll();
    if (old != scroll) {
        offset = scroll;
        hit_count = 0;
        gui_round_rect(screen, (GuiRect){0, 0, view.width, view.height}, 0, rgb(244, 246, 250), 255);
        if (query[0])
            results();
        else if (page == HOME)
            home();
        else
            details();
    }
    screen = boot;
    offset = 0;
    if (maximum_scroll()) {
        fill(scroll_track, 3, rgb(221, 228, 236));
        int thumb = view.height * view.height / content_height;
        if (thumb < 24)
            thumb = 24;
        int top = (view.height - thumb) * scroll / maximum_scroll();
        fill((GuiRect){scroll_track.x, scroll_track.y + top, 6, thumb}, 3, rgb(120, 140, 159));
    }
    clipped(view.x, w.y + w.height - 29, view.width, settings_status(), GUI_SMALL, muted());
}
static void option(unsigned i) {
    const DesktopSettings *s = settings_current();
    if (page == 0 && i < 4)
        settings_resolution(i);
    else if (page == 1) {
        if (i < 3)
            settings_wallpaper(i);
        else if (i < 5)
            settings_theme(i - 3);
    } else if (page == 2 && i < 4) {
        if (i == 0)
            settings_volume(s->volume >= 10 ? s->volume - 10 : 0);
        if (i == 1)
            settings_volume(s->volume <= 90 ? s->volume + 10 : 100);
        if (i == 2)
            settings_volume(s->volume ? 0 : 100);
        if (i == 3 && device)
            platform_audio_start(device);
        platform_audio_volume(settings_current()->volume);
    } else if (page == 3 && i < 7)
        settings_zone(i);
}
static void scroll_to(int y) {
    int max = maximum_scroll();
    scroll = max ? max * (y - scroll_track.y) / scroll_track.height : 0;
    clamp_scroll();
}
static void click(GuiRect w, int x, int y) {
    layout(w);
    if (inside(search_box, x, y)) {
        query_selected = 0;
        if (x >= search_box.x + search_box.width - 30) {
            query[0] = 0;
            scroll = 0;
        }
        searching = 1;
        return;
    }
    searching = 0;
    unsigned order[7] = {HOME, 0, 1, 2, 3, 5, 4};
    for (unsigned i = 0; i < 7; ++i)
        if (inside((GuiRect){w.x + 12, w.y + 146 + (int)i * 38, view.x - w.x - 36, 34}, x, y)) {
            settings_app_select(order[i]);
            return;
        }
    if (x >= scroll_track.x - 5 && x < scroll_track.x + 12 && y >= scroll_track.y &&
        y < scroll_track.y + scroll_track.height && maximum_scroll()) {
        drag_scroll = 1;
        scroll_to(y);
        return;
    }
    if (!inside(view, x, y))
        return;
    int local_x = x - view.x, local_y = y - view.y + scroll;
    for (unsigned i = 0; i < hit_count; ++i)
        if (inside(hits[i].rect, local_x, local_y)) {
            int a = hits[i].action;
            if (a < 100)
                settings_app_select((unsigned)a);
            else if (a < 200)
                settings_wallpaper((unsigned)(a - 100));
            else
                option((unsigned)(a - 200));
            return;
        }
}
static void pointer(GuiRect w, int x, int y, unsigned buttons) {
    (void)x;
    layout(w);
    if (!(buttons & 1))
        drag_scroll = 0;
    else if (drag_scroll)
        scroll_to(y);
}
static void key(char k) {
    unsigned code = (unsigned char)k;
    if (k == '/' && !searching) {
        searching = 1;
        return;
    }
    if (searching) {
        unsigned n = (unsigned)length(query);
        if (code == EDIT_SELECT_ALL) {
            query_selected = 1;
            return;
        }
        if (k == '\b' && n) {
            query[query_selected ? 0 : n - 1] = 0;
            query_selected = 0;
            scroll = 0;
            return;
        }
        if (k == '\n' && query[0]) {
            for (unsigned i = 0; i < 6; ++i)
                if (contains(keywords[i], query) || contains(names[i], query)) {
                    settings_app_select(i);
                    return;
                }
        }
        if (k == '\n' || k == '\t') {
            searching = 0;
            return;
        }
        if (code >= 32 && code <= 126 && (query_selected || n + 1 < sizeof(query))) {
            if (query_selected)
                n = 0;
            query_selected = 0;
            query[n] = k;
            query[n + 1] = 0;
            scroll = 0;
        }
        return;
    }
    if (code == EDIT_PAGE_DOWN || code == EDIT_DOWN) {
        scroll += code == EDIT_DOWN ? 36 : view.height - 30;
        clamp_scroll();
        return;
    }
    if (code == EDIT_PAGE_UP || code == EDIT_UP) {
        scroll -= code == EDIT_UP ? 36 : view.height - 30;
        clamp_scroll();
        return;
    }
    if (code == EDIT_HOME) {
        scroll = 0;
        return;
    }
    if (code == EDIT_END) {
        scroll = maximum_scroll();
        return;
    }
    if (k == '\t') {
        settings_app_select(page == HOME ? 0 : page == 5 ? HOME : page + 1);
        return;
    }
    if (k >= '1' && k <= '6') {
        settings_app_select((unsigned)(k - '1'));
        return;
    }
    if (k == '7') {
        settings_app_select(HOME);
        return;
    }
    if (k >= 'a' && k <= 'g')
        option((unsigned)(k - 'a'));
}
static int escape(void) {
    if (searching || query[0]) {
        searching = 0;
        query[0] = 0;
        scroll = 0;
        return 1;
    }
    return 0;
}
static void open(const AppServices *services) {
    (void)services;
    drag_scroll = 0;
}
const AppDefinition settings_application = {APP_SETTINGS, "Settings", 640,     450,    open, draw,
                                            key,          click,      pointer, escape, 0};
