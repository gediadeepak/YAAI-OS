#include "quick_settings.h"
#include "edit_keys.h"
#include "platform/platform.h"
#include "settings.h"
#include "shell_ui.h"
static int minimum(int a, int b) { return a < b ? a : b; }
static int inside(GuiRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
QuickInfo quick_settings_info(void) {
    QuickInfo i = {0};
    i.clock_ready = clock_local_time(settings_current()->zone, &i.now);
    i.audio = platform_audio_available();
    i.network = platform_network_status();
    i.address = platform_network_address();
    i.wireless = platform_wireless_status();
    return i;
}
int quick_calendar_days(int y, int m) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970 || y > 9999 || m < 1 || m > 12)
        return 0;
    return days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
int quick_calendar_first(int y, int m) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (!quick_calendar_days(y, m))
        return -1;
    if (m < 3)
        --y;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + 1) % 7;
}
void quick_settings_open(QuickSettings *s, const QuickInfo *i) {
    *s = (QuickSettings){0};
    s->restore_volume = settings_current()->volume ? settings_current()->volume : 50;
    if (i->clock_ready && quick_calendar_days(i->now.year, i->now.month)) {
        s->year = i->now.year;
        s->month = i->now.month;
    }
}
void quick_settings_cancel(QuickSettings *s) {
    if (s->dragging)
        platform_audio_volume(settings_current()->volume);
    s->dragging = 0;
}
GuiRect quick_settings_bounds(unsigned w, unsigned h) {
    int split = w >= 640 && h < 820;
    int width = minimum((int)w - 24, split ? 760 : 440), height = minimum((int)h - 24, split ? 600 : 740);
    int x = w >= 900 ? (int)w - width - 24 : ((int)w - width) / 2, y = split ? 12 : 104;
    if (y + height > (int)h - 12)
        y = (int)h - height - 12;
    return (GuiRect){x, y, width, height};
}
static int split(GuiRect b) { return b.width > 500; }
static GuiRect column(GuiRect b) {
    int w = split(b) ? (b.width - 56) / 2 : b.width - 40;
    return (GuiRect){b.x + 20, b.y + 82, w, 94};
}
static GuiRect sound_rect(GuiRect b) {
    GuiRect r = column(b);
    r.y = b.y + 186;
    r.height = 90;
    return r;
}
static GuiRect calendar_rect(GuiRect b) {
    GuiRect c = column(b);
    return split(b) ? (GuiRect){c.x + c.width + 16, b.y + 82, c.width, b.height - 164}
                    : (GuiRect){c.x, b.y + 372, c.width, b.height - 454};
}
GuiRect quick_settings_control(GuiRect b, unsigned n) {
    GuiRect v = sound_rect(b), c = column(b), cal = calendar_rect(b);
    if (n == 0)
        return (GuiRect){v.x + 8, v.y + 32, 44, 44};
    if (n == 1)
        return (GuiRect){v.x + 64, v.y + 32, v.width - 80, 44};
    if (n >= 2 && n <= 4) {
        int w = (c.width - 12) / 3;
        return (GuiRect){c.x + (int)(n - 2) * (w + 6), b.y + 310, w, 44};
    }
    if (n == 5)
        return (GuiRect){cal.x, cal.y, 44, 44};
    if (n == 6)
        return (GuiRect){cal.x + 44, cal.y, cal.width - 88, 44};
    if (n == 7)
        return (GuiRect){cal.x + cal.width - 44, cal.y, 44, 44};
    if (n >= 8 && n <= 10) {
        int w = (b.width - 52) / 3;
        return (GuiRect){b.x + 20 + (int)(n - 8) * (w + 6), b.y + b.height - 62, w, 44};
    }
    if (n == 11)
        return (GuiRect){b.x + b.width - 60, b.y + 14, 44, 44};
    return (GuiRect){0};
}
int quick_settings_hit(GuiRect b, int x, int y) {
    if (!inside(b, x, y))
        return -1;
    for (unsigned n = 0; n < QUICK_CONTROLS; ++n)
        if (inside(quick_settings_control(b, n), x, y))
            return (int)n;
    return -1;
}
static void change_month(QuickSettings *s, int delta) {
    if (!quick_calendar_days(s->year, s->month))
        return;
    int year = s->year, month = s->month + delta;
    if (month < 1) {
        month = 12;
        --year;
    }
    if (month > 12) {
        month = 1;
        ++year;
    }
    if (quick_calendar_days(year, month)) {
        s->year = year;
        s->month = month;
    }
}
static void set_volume(QuickSettings *s, int value) {
    if (value < 0)
        value = 0;
    if (value > 100)
        value = 100;
    if (value)
        s->restore_volume = (unsigned)value;
    settings_volume((unsigned)value);
    platform_audio_volume((unsigned)value);
}
static int action(QuickSettings *s, const QuickInfo *i, unsigned n) {
    if (n == 0 && i->audio) {
        unsigned v = settings_current()->volume;
        if (v)
            s->restore_volume = v;
        set_volume(s, v ? 0 : (int)s->restore_volume);
    }
    if (n >= 2 && n <= 4)
        settings_color_mode(n - 2);
    if (n == 5)
        change_month(s, -1);
    if (n == 6 && i->clock_ready) {
        s->year = i->now.year;
        s->month = i->now.month;
    }
    if (n == 7)
        change_month(s, 1);
    if (n == 8)
        return QUICK_SETTINGS;
    if (n == 9)
        return QUICK_RESTART;
    if (n == 10)
        return QUICK_SHUTDOWN;
    if (n == 11)
        return QUICK_CLOSE;
    return 0;
}
static unsigned slider_value(GuiRect b, int x) {
    GuiRect r = quick_settings_control(b, 1);
    int v = (x - r.x) * 100 / (r.width - 1);
    return v < 0 ? 0 : v > 100 ? 100 : (unsigned)v;
}
int quick_settings_click(QuickSettings *s, GuiRect b, const QuickInfo *i, int x, int y) {
    int n = quick_settings_hit(b, x, y);
    if (n < 0)
        return 0;
    s->focus = n;
    if (n == 1 && i->audio) {
        s->dragging = 1;
        s->preview = slider_value(b, x);
        platform_audio_volume(s->preview);
        return 0;
    }
    return action(s, i, (unsigned)n);
}
int quick_settings_pointer(QuickSettings *s, GuiRect b, const QuickInfo *i, int x, int y,
                           unsigned buttons) {
    (void)y;
    if (!s->dragging)
        return 0;
    if (!i->audio) {
        quick_settings_cancel(s);
        return 1;
    }
    if (!(buttons & 1)) {
        set_volume(s, (int)s->preview);
        s->dragging = 0;
        return 1;
    }
    unsigned v = slider_value(b, x);
    if (v == s->preview)
        return 0;
    s->preview = v;
    platform_audio_volume(v);
    return 1;
}
int quick_settings_key(QuickSettings *s, const QuickInfo *i, char key) {
    unsigned k = (unsigned char)key;
    if (key == 27) {
        quick_settings_cancel(s);
        return QUICK_CLOSE;
    }
    if (key == '\t') {
        s->focus = (s->focus + 1) % QUICK_CONTROLS;
        return 0;
    }
    if (key == 'm' || key == 'M')
        return action(s, i, 0);
    if (key == '\n' || key == ' ')
        return action(s, i, (unsigned)s->focus);
    if (s->focus == 1 && (k == EDIT_LEFT || k == EDIT_RIGHT || key == '+' || key == '-')) {
        if (i->audio)
            set_volume(s, (int)settings_current()->volume + ((k == EDIT_LEFT || key == '-') ? -5 : 5));
        return 0;
    }
    if ((s->focus >= 5 && s->focus <= 7) && (k == EDIT_LEFT || k == EDIT_RIGHT)) {
        change_month(s, k == EDIT_LEFT ? -1 : 1);
        return 0;
    }
    if (k == EDIT_LEFT || k == EDIT_UP)
        s->focus = (s->focus + QUICK_CONTROLS - 1) % QUICK_CONTROLS;
    if (k == EDIT_RIGHT || k == EDIT_DOWN)
        s->focus = (s->focus + 1) % QUICK_CONTROLS;
    return 0;
}
static void number(char *out, unsigned v, unsigned pad) {
    char t[12];
    unsigned n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n < pad)
        t[n++] = '0';
    unsigned j = 0;
    while (n)
        out[j++] = t[--n];
    out[j] = 0;
}
static void append(char *to, const char *s) {
    unsigned n = 0;
    while (to[n])
        ++n;
    for (unsigned j = 0; s[j] && n < 95; ++j)
        to[n++] = s[j];
    to[n] = 0;
}
static void text(const BootInfo *s, int x, int y, const char *label, uint32_t color) {
    gui_text(s, x, y, label, GUI_SMALL, color);
}
static void clipped(const BootInfo *s, int x, int y, int width, const char *label, uint32_t color) {
    char value[128];
    unsigned n = 0;
    if (!label)
        label = "Unavailable";
    for (; label[n] && n + 1 < sizeof(value); ++n) {
        value[n] = label[n];
        value[n + 1] = 0;
        if (gui_text_width(value, GUI_SMALL) > width) {
            value[n] = 0;
            break;
        }
    }
    value[n] = 0;
    text(s, x, y, value, color);
}
static void centered(const BootInfo *s, GuiRect b, int y, const char *label, uint32_t color) {
    text(s, b.x + (b.width - gui_text_width(label, GUI_SMALL)) / 2, y, label, color);
}
void quick_settings_draw(const BootInfo *screen, const QuickSettings *s, const QuickInfo *i, GuiRect b) {
    ShellPalette p = shell_palette(screen);
    gui_shadow(screen, b, 22);
    gui_round_rect(screen, b, 22, p.border, 255);
    gui_round_rect(screen, (GuiRect){b.x + 1, b.y + 1, b.width - 2, b.height - 2}, 21, p.background, 255);
    gui_text(screen, b.x + 24, b.y + 20, "Quick settings", GUI_BODY, p.text);
    char date[96] = "", v[12];
    if (i->clock_ready) {
        number(date, (unsigned)i->now.year, 4);
        append(date, "/");
        number(v, (unsigned)i->now.month, 2);
        append(date, v);
        append(date, "/");
        number(v, (unsigned)i->now.day, 2);
        append(date, v);
        append(date, "    ");
        number(v, (unsigned)i->now.hour, 2);
        append(date, v);
        append(date, ":");
        number(v, (unsigned)i->now.minute, 2);
        append(date, v);
    } else
        append(date, "Clock unavailable");
    text(screen, b.x + 24, b.y + 52, date, p.muted);
    GuiRect net = column(b), sound = sound_rect(b), cal = calendar_rect(b);
    gui_round_rect(screen, net, 12, p.surface, 255);
    text(screen, net.x + 12, net.y + 10, "Connectivity", p.text);
    clipped(screen, net.x + 12, net.y + 30, net.width - 24, i->network, p.muted);
    char ip[96] = "IP: ";
    append(ip, i->address ? i->address : "unavailable");
    clipped(screen, net.x + 12, net.y + 49, net.width - 24, ip, p.muted);
    clipped(screen, net.x + 12, net.y + 69, net.width - 24, i->wireless, p.muted);
    gui_round_rect(screen, sound, 12, p.surface, 255);
    text(screen, sound.x + 12, sound.y + 10, "Sound", p.text);
    unsigned volume = s->dragging ? s->preview : settings_current()->volume;
    char audio[96] = "";
    if (i->audio) {
        number(audio, volume, 1);
        append(audio, volume ? "%" : "% / Muted");
    } else
        append(audio, "Unavailable on this device");
    clipped(screen, sound.x + 74, sound.y + 10, sound.width - 86, audio, p.muted);
    GuiRect mute = quick_settings_control(b, 0), slider = quick_settings_control(b, 1);
    gui_round_rect(screen, mute, 10, volume ? p.background : p.hover, 255);
    centered(screen, mute, mute.y + 15, volume ? "Mute" : "On", i->audio ? p.text : p.muted);
    int cy = slider.y + 22;
    gui_line(screen, slider.x, cy, slider.x + slider.width - 1, cy, 4, p.border);
    if (i->audio) {
        int x = slider.x + (int)volume * (slider.width - 1) / 100;
        gui_line(screen, slider.x, cy, x, cy, 4, p.accent);
        gui_round_rect(screen, (GuiRect){x - 8, cy - 8, 16, 16}, 8, p.accent, 255);
    }
    GuiRect c = column(b);
    text(screen, c.x, b.y + 288, "Appearance", p.muted);
    const char *modes[] = {"System", "Light", "Dark"};
    for (unsigned n = 0; n < 3; ++n) {
        GuiRect r = quick_settings_control(b, n + 2);
        int active = settings_current()->color_mode == n;
        gui_round_rect(screen, r, 10, active ? p.hover : p.surface, 255);
        centered(screen, r, r.y + 15, modes[n], p.text);
        if (active)
            gui_line(screen, r.x + 12, r.y + r.height - 3, r.x + r.width - 12, r.y + r.height - 3, 2,
                     p.accent);
    }
    clipped(screen, c.x, b.y + 356, c.width, settings_status(), p.muted);
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    gui_round_rect(screen, cal, 12, p.surface, 255);
    if (quick_calendar_days(s->year, s->month)) {
        char title[96] = "";
        append(title, months[s->month - 1]);
        append(title, " ");
        number(v, (unsigned)s->year, 4);
        append(title, v);
        GuiRect prev = quick_settings_control(b, 5), next = quick_settings_control(b, 7);
        centered(screen, prev, prev.y + 15, "<", p.text);
        centered(screen, next, next.y + 15, ">", p.text);
        centered(screen, quick_settings_control(b, 6), cal.y + 15, title, p.text);
        const char *days[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
        int cell = (cal.width - 12) / 7, row = (cal.height - 66) / 6;
        for (int d = 0; d < 7; ++d)
            centered(screen, (GuiRect){cal.x + 6 + d * cell, cal.y + 46, cell, 18}, cal.y + 46, days[d],
                     p.muted);
        int first = quick_calendar_first(s->year, s->month), count = quick_calendar_days(s->year, s->month);
        for (int day = 1; day <= count; ++day) {
            int pos = first + day - 1;
            GuiRect r = {cal.x + 6 + (pos % 7) * cell, cal.y + 64 + (pos / 7) * row, cell, row};
            if (i->clock_ready && s->year == i->now.year && s->month == i->now.month && day == i->now.day)
                gui_round_rect(screen, (GuiRect){r.x + 2, r.y, r.width - 4, r.height}, row / 2, p.hover,
                               255);
            number(v, (unsigned)day, 1);
            centered(screen, r, r.y + (row - 14) / 2, v, p.text);
        }
    } else
        centered(screen, cal, cal.y + 60, "Calendar unavailable", p.muted);
    const char *footer[] = {"Settings", "Restart", "Shut down"};
    gui_line(screen, b.x + 20, b.y + b.height - 74, b.x + b.width - 20, b.y + b.height - 74, 1, p.border);
    for (unsigned n = 0; n < 3; ++n) {
        GuiRect r = quick_settings_control(b, n + 8);
        gui_round_rect(screen, r, 10, p.surface, 255);
        centered(screen, r, r.y + 15, footer[n], p.text);
    }
    GuiRect close = quick_settings_control(b, 11);
    centered(screen, close, close.y + 15, "x", p.muted);
    if (s->focus >= 0 && s->focus < QUICK_CONTROLS) {
        GuiRect r = quick_settings_control(b, (unsigned)s->focus);
        gui_line(screen, r.x + 6, r.y + r.height - 1, r.x + r.width - 6, r.y + r.height - 1, 2, p.accent);
    }
}