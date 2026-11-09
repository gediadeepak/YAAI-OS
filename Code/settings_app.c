#include "settings_app.h"
#include "apps.h"
#include "brand.h"
#include "edit_keys.h"
#include "memory.h"
#include "platform/platform.h"
#include "screen.h"
#include "settings.h"
static unsigned page;
static const BootInfo *screen;
static const char *pages[] = {"Display", "Appearance", "Sound", "Date & time", "About", "Devices"};
static unsigned selected(unsigned option) {
    const DesktopSettings *s = settings_current();
    return page == 0   ? s->resolution == option
           : page == 1 ? (option < 3 ? s->wallpaper == option : s->theme == option - 3)
           : page == 2 ? s->volume == option
           : page == 3 ? s->zone == option
                       : 0;
}
void settings_app_select(unsigned value) {
    if (value < 6)
        page = value;
}
static uint32_t rgb(unsigned r, unsigned g, unsigned b) {
    return color(screen, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static uint32_t accent(void) { return settings_current()->theme ? rgb(186, 146, 75) : rgb(55, 133, 145); }
static void text(int x, int y, const char *s, GuiFont font) {
    gui_text(screen, x, y, s, font, rgb(216, 232, 244));
}
static int inside(GuiRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
static GuiRect content(GuiRect w) { return (GuiRect){w.x + 184, w.y + 70, w.width - 208, w.height - 116}; }
static GuiRect button(GuiRect w, unsigned i) {
    GuiRect c = content(w);
    if (page == 3)
        return (GuiRect){c.x + (int)(i % 2) * (c.width + 8) / 2, c.y + 68 + (int)(i / 2) * 43,
                         (c.width - 8) / 2, 35};
    return (GuiRect){c.x + (int)(i % 2) * (c.width + 8) / 2, c.y + 76 + (int)(i / 2) * 52, (c.width - 8) / 2,
                     42};
}
static void choice(GuiRect w, unsigned i, const char *caption, int on) {
    GuiRect r = button(w, i);
    gui_round_rect(screen, r, 9, on ? accent() : rgb(32, 49, 68), 255);
    text(r.x + 12, r.y + 11, caption, GUI_SMALL);
}
static void value(int x, int y, uint64_t n, const char *suffix) {
    char s[64], rev[24];
    unsigned at = 0, k = 0;
    do {
        rev[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (k)
        s[at++] = rev[--k];
    for (unsigned j = 0; suffix[j] && at < 63; ++j)
        s[at++] = suffix[j];
    s[at] = 0;
    text(x, y, s, GUI_BODY);
}
static void action(unsigned option) {
    const DesktopSettings *s = settings_current();
    if (page == 0 && option < 4)
        settings_resolution(option);
    else if (page == 1) {
        if (option < 3)
            settings_wallpaper(option);
        else if (option < 5)
            settings_theme(option - 3);
    } else if (page == 2) {
        if (option == 0)
            settings_volume(s->volume >= 10 ? s->volume - 10 : 0);
        if (option == 1)
            settings_volume(s->volume <= 90 ? s->volume + 10 : 100);
        if (option == 2)
            settings_volume(s->volume ? 0 : 100);
        platform_audio_volume(settings_current()->volume);
        if (option == 3 && screen) {
            platform_audio_start(screen);
            platform_audio_volume(settings_current()->volume);
        }
    } else if (page == 3 && option < 7)
        settings_zone(option);
}
static void draw(const BootInfo *boot, GuiRect w) {
    screen = boot;
    GuiRect c = content(w);
    gui_round_rect(screen, (GuiRect){w.x + 12, w.y + 63, 156, w.height - 78}, 12, rgb(18, 32, 49), 255);
    for (unsigned i = 0; i < 6; ++i) {
        GuiRect r = {w.x + 20, w.y + 76 + (int)i * 42, 140, 36};
        if (page == i)
            gui_round_rect(screen, r, 8, accent(), 255);
        text(r.x + 12, r.y + 10, pages[i], GUI_SMALL);
    }
    text(c.x, c.y, pages[page], GUI_HEADING);
    const DesktopSettings *s = settings_current();
    if (page == 0) {
        text(c.x, c.y + 43, "Resolution - applies after restart", GUI_SMALL);
        const char *names[] = {"Auto (recommended)", "1024 x 768", "1280 x 720", "1920 x 1080"};
        for (unsigned i = 0; i < 4; ++i)
            choice(w, i, names[i], selected(i));
        text(c.x, c.y + 194, "Current display:", GUI_SMALL);
        value(c.x, c.y + 217, boot->width, " pixels wide");
        value(c.x, c.y + 244, boot->height, " pixels high");
        text(c.x, c.y + 278, "UI scaling: 100% (fixed)", GUI_SMALL);
        text(c.x, c.y + 303, "Unavailable modes fall back to Auto.", GUI_SMALL);
    } else if (page == 1) {
        text(c.x, c.y + 43, "Wallpaper and accent theme", GUI_SMALL);
        const char *names[] = {"AI human", "Midnight", "Teal", "Ocean accent", "Amber accent"};
        for (unsigned i = 0; i < 5; ++i)
            choice(w, i, names[i], selected(i));
        text(c.x, c.y + 251, "Changes apply immediately.", GUI_SMALL);
    } else if (page == 2) {
        text(c.x, c.y + 43,
             platform_audio_available() ? "Stereo output available" : "No supported sound output detected",
             GUI_SMALL);
        choice(w, 0, "-  Volume", 0);
        choice(w, 1, "+  Volume", 0);
        choice(w, 2, s->volume ? "Mute" : "Unmute", !s->volume);
        choice(w, 3, "Play test sound", 0);
        value(c.x, c.y + 202, s->volume, "% volume");
        text(c.x, c.y + 244, "Volume is saved automatically.", GUI_SMALL);
    } else if (page == 3) {
        text(c.x, c.y + 43, "Hardware clock: UTC", GUI_SMALL);
        for (unsigned i = 0; i < 7; ++i)
            choice(w, i, setup_zone_name(i), selected(i));
        DateTime now;
        if (clock_local_time(s->zone, &now)) {
            char stamp[20] = "0000-00-00 00:00:00";
            unsigned fields[6] = {(unsigned)now.year % 100, (unsigned)now.month,  (unsigned)now.day,
                                  (unsigned)now.hour,       (unsigned)now.minute, (unsigned)now.second};
            const unsigned positions[6] = {2, 5, 8, 11, 14, 17};
            stamp[0] = (char)('0' + (unsigned)now.year / 1000 % 10);
            stamp[1] = (char)('0' + (unsigned)now.year / 100 % 10);
            for (unsigned i = 0; i < 6; ++i) {
                stamp[positions[i]] = (char)('0' + fields[i] / 10);
                stamp[positions[i] + 1] = (char)('0' + fields[i] % 10);
            }
            text(c.x, c.y + 255, stamp, GUI_BODY);
        }
        text(c.x, c.y + 294, "Country rules handle daylight saving.", GUI_SMALL);
    } else if (page == 4) {
        text(c.x, c.y + 48, OS_NAME, GUI_BODY);
        text(c.x, c.y + 79, OS_TAGLINE, GUI_SMALL);
        text(c.x, c.y + 110, "VERSION I", GUI_SMALL);
        text(c.x, c.y + 143, platform_name(), GUI_SMALL);
        MemoryStats m = memory_stats();
        value(c.x, c.y + 178, (m.usable_pages + m.retained_pages + m.limited_pages) / 256, " MiB total RAM");
        value(c.x, c.y + 209, m.free_pages / 256, " MiB available RAM");
        FileUsage u;
        if (files_usage(&u) == FILE_OK) {
            value(c.x, c.y + 240, u.used_bytes, " document bytes used");
        }
        if (setup_valid(&boot->setup))
            value(c.x, c.y + 278, boot->setup.data_sectors / 2048, " MiB data storage");
        else
            text(c.x, c.y + 278, "Live session / storage not installed", GUI_SMALL);
    } else {
        text(c.x, c.y + 49, platform_network_status(), GUI_SMALL);
        text(c.x, c.y + 81, platform_network_address(), GUI_SMALL);
        text(c.x, c.y + 113, platform_wireless_status(), GUI_SMALL);
        text(c.x, c.y + 145, "SIM: modem support not implemented", GUI_SMALL);
        PlatformPower p = platform_power();
        text(c.x, c.y + 177,
             !p.supported ? "Battery reporting unavailable"
             : !p.present ? "No battery reported"
             : p.charging ? "Battery charging"
                          : "Battery present",
             GUI_SMALL);
        if (p.percent >= 0)
            value(c.x, c.y + 209, (unsigned)p.percent, "% battery");
        text(c.x, c.y + 253, platform_audio_available() ? "Sound output ready" : "Sound output unavailable",
             GUI_SMALL);
    }
    text(c.x, w.y + w.height - 31, settings_status(), GUI_SMALL);
}
static void click(GuiRect w, int x, int y) {
    for (unsigned i = 0; i < 6; ++i)
        if (inside((GuiRect){w.x + 20, w.y + 76 + (int)i * 42, 140, 36}, x, y)) {
            page = i;
            return;
        }
    unsigned count = page == 0 ? 4 : page == 1 ? 5 : page == 2 ? 4 : page == 3 ? 7 : 0;
    for (unsigned i = 0; i < count; ++i)
        if (inside(button(w, i), x, y)) {
            action(i);
            return;
        }
}
static void key(char k) {
    if (k == '\t') {
        page = (page + 1) % 6;
        return;
    }
    if (k >= '1' && k <= '6') {
        page = (unsigned)(k - '1');
        return;
    }
    if (k >= 'a' && k <= 'g')
        action((unsigned)(k - 'a'));
}
const AppDefinition settings_application = {APP_SETTINGS, "Settings", 640, 450, 0, draw, key, click, 0, 0, 0};
