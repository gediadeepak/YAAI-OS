#include "shell_ui.h"
#include "edit_keys.h"
#include "screen.h"
#include "settings.h"
static const ShellApp app_list[SHELL_APP_COUNT] = {
    {6, 9, "Files", "F4", "files folders documents browse storage"},
    {3, 17, "Notes", "F12", "notes text editor write document"},
    {2, 3, "Settings", "F2", "settings system preferences appearance display sound time"},
    {1, 2, "Terminal", "F1", "terminal console command shell"},
    {5, 15, "Calculator", "F11", "calculator arithmetic numbers maths"},
    {4, 12, "About", "F5", "about version memory information yaai"}};
const ShellApp *shell_app(unsigned i) { return i < SHELL_APP_COUNT ? &app_list[i] : 0; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
static int matches(const char *s, const char *q) {
    if (!q[0])
        return 1;
    for (unsigned i = 0; s[i]; ++i) {
        unsigned k = 0;
        while (q[k] && s[i + k] && lower(q[k]) == lower(s[i + k]))
            ++k;
        if (!q[k])
            return 1;
    }
    return 0;
}
unsigned shell_launcher_count(const ShellLauncher *s) {
    unsigned n = 0;
    if (!s)
        return 0;
    for (unsigned i = 0; i < SHELL_APP_COUNT; ++i)
        if (matches(app_list[i].keywords, s->query) || matches(app_list[i].name, s->query))
            ++n;
    return n;
}
const ShellApp *shell_launcher_app(const ShellLauncher *s, unsigned index) {
    if (!s)
        return 0;
    for (unsigned i = 0; i < SHELL_APP_COUNT; ++i)
        if (matches(app_list[i].keywords, s->query) || matches(app_list[i].name, s->query)) {
            if (!index)
                return &app_list[i];
            --index;
        }
    return 0;
}
void shell_launcher_init(ShellLauncher *s) {
    if (s)
        *s = (ShellLauncher){0};
}
static void search_changed(ShellLauncher *s) {
    s->selected = shell_launcher_count(s) ? 0 : -1;
    s->select_all = 0;
}
int shell_launcher_key(ShellLauncher *s, char key) {
    if (!s)
        return 0;
    unsigned k = (unsigned char)key, n = shell_launcher_count(s);
    if (key == 27) {
        if (s->query[0]) {
            shell_launcher_init(s);
            return 0;
        }
        return SHELL_CLOSE;
    }
    if (k == EDIT_SELECT_ALL) {
        s->select_all = 1;
        return 0;
    }
    if (key == '\b') {
        unsigned count = 0;
        while (s->query[count])
            ++count;
        if (count)
            s->query[s->select_all ? 0 : count - 1] = 0;
        search_changed(s);
        return 0;
    }
    if (key == '\n') {
        if (s->selected < 0)
            return 0;
        if ((unsigned)s->selected < n) {
            const ShellApp *app = shell_launcher_app(s, (unsigned)s->selected);
            return app ? app->target : 0;
        }
        return (unsigned)s->selected == n ? 13 : 14;
    }
    if (k == EDIT_HOME) {
        s->selected = n ? 0 : -1;
        return 0;
    }
    if (k == EDIT_END) {
        s->selected = (int)n + 1;
        return 0;
    }
    if (k == EDIT_PAGE_DOWN) {
        s->selected = (int)n;
        return 0;
    }
    if (k == EDIT_PAGE_UP) {
        s->selected = n ? 0 : -1;
        return 0;
    }
    if (key == '\t' || k == EDIT_RIGHT || k == EDIT_DOWN || k == EDIT_LEFT || k == EDIT_UP) {
        int step = k == EDIT_LEFT ? -1 : k == EDIT_UP ? -3 : k == EDIT_DOWN ? 3 : 1, total = (int)n + 2;
        if (s->selected < 0)
            s->selected = 0;
        else
            s->selected = (s->selected + step % total + total) % total;
        return 0;
    }
    if (k == 1)
        return 2;
    if (k == 2)
        return 3;
    if (k == 3)
        return SHELL_DEVICES;
    if (k == 4)
        return 9;
    if (k == 5)
        return 12;
    if (k == 20)
        return 15;
    if (k == 30)
        return 17;
    if (k >= 32 && k <= 126) {
        unsigned count = 0;
        while (s->query[count])
            ++count;
        if (s->select_all)
            count = 0;
        if (count + 1 < sizeof(s->query)) {
            s->query[count] = key;
            s->query[count + 1] = 0;
            search_changed(s);
        }
        return 0;
    }
    return 0;
}
static int inside(GuiRect r, int x, int y) {
    return r.width > 0 && r.height > 0 && x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
static int minimum(int a, int b) { return a < b ? a : b; }
GuiRect shell_launcher_bounds(unsigned width, unsigned height, GuiRect dock) {
    (void)height;
    int w = minimum(560, (int)width - 24), h = minimum(500, dock.y - 24);
    if (h < 1)
        h = 1;
    return (GuiRect){((int)width - w) / 2, dock.y - h - 12, w, h};
}
static GuiRect search_rect(GuiRect b) {
    return (GuiRect){b.x + 24, b.y + (b.height < 440 ? 58 : 76), b.width - 48, b.height < 440 ? 36 : 42};
}
GuiRect shell_launcher_tile(GuiRect b, unsigned index) {
    if (index >= SHELL_APP_COUNT)
        return (GuiRect){0};
    int compact = b.height < 440, width = (b.width - 64) / 3, top = compact ? 126 : 160,
        step = compact ? 74 : 112;
    return (GuiRect){b.x + 24 + (int)(index % 3) * (width + 8), b.y + top + (int)(index / 3) * step, width,
                     compact ? 68 : 100};
}
static GuiRect power_rect(GuiRect b, int shutdown) {
    int width = (b.width - 64) / 2;
    return (GuiRect){b.x + 24 + shutdown * (width + 16), b.y + b.height - 62, width, 42};
}
int shell_launcher_hit(const ShellLauncher *s, GuiRect b, int x, int y) {
    if (!s || !inside(b, x, y))
        return 0;
    GuiRect search = search_rect(b);
    if (inside(search, x, y))
        return s->query[0] && x >= search.x + search.width - 32 ? SHELL_CLEAR : SHELL_SEARCH;
    unsigned count = shell_launcher_count(s);
    for (unsigned i = 0; i < count; ++i)
        if (inside(shell_launcher_tile(b, i), x, y)) {
            const ShellApp *app = shell_launcher_app(s, i);
            return app ? app->target : 0;
        }
    if (inside(power_rect(b, 0), x, y))
        return 13;
    if (inside(power_rect(b, 1), x, y))
        return 14;
    return 0;
}
int shell_launcher_hover(ShellLauncher *s, GuiRect b, int x, int y) {
    if (!s)
        return 0;
    int target = shell_launcher_hit(s, b, x, y), before = s->selected;
    unsigned count = shell_launcher_count(s);
    for (unsigned i = 0; i < count; ++i) {
        const ShellApp *app = shell_launcher_app(s, i);
        if (app && app->target == target)
            s->selected = (int)i;
    }
    if (target == 13)
        s->selected = (int)count;
    if (target == 14)
        s->selected = (int)count + 1;
    return before != s->selected;
}
static uint32_t rgb(const BootInfo *s, unsigned r, unsigned g, unsigned b) {
    return color(s, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
ShellPalette shell_palette(const BootInfo *s) {
    int dark = settings_dark();
    return (ShellPalette){dark ? rgb(s, 14, 24, 40) : rgb(s, 240, 244, 250),
                          dark ? rgb(s, 46, 65, 87) : rgb(s, 204, 216, 231),
                          dark ? rgb(s, 25, 42, 62) : rgb(s, 255, 255, 255),
                          dark ? rgb(s, 35, 65, 82) : rgb(s, 218, 234, 243),
                          dark ? rgb(s, 229, 239, 249) : rgb(s, 31, 45, 63),
                          dark ? rgb(s, 153, 176, 197) : rgb(s, 99, 117, 139),
                          settings_current()->theme ? (dark ? rgb(s, 232, 195, 119) : rgb(s, 162, 115, 36))
                                                    : (dark ? rgb(s, 105, 218, 221) : rgb(s, 0, 117, 140))};
}
typedef struct {
    const BootInfo *s;
    GuiRect b;
    uint32_t tile, fg;
} Icon;
static int x(Icon *i, int n) { return i->b.x + n * i->b.width / 48; }
static int y(Icon *i, int n) { return i->b.y + n * i->b.height / 48; }
static void rect(Icon *i, int left, int top, int w, int h, int radius, uint32_t c) {
    gui_round_rect(i->s, (GuiRect){x(i, left), y(i, top), w * i->b.width / 48, h * i->b.height / 48},
                   radius * i->b.width / 48, c, 255);
}
static void stroke(Icon *i, int a, int b, int c, int d, int width, uint32_t fill) {
    int thickness = width * i->b.width / 48;
    if (thickness < 1)
        thickness = 1;
    gui_line(i->s, x(i, a), y(i, b), x(i, c), y(i, d), thickness, fill);
}
void shell_icon(const BootInfo *s, unsigned id, GuiRect box) {
    ShellPalette p = shell_palette(s);
    uint32_t tile, fg;
    switch (id) {
    case 0:
        tile = settings_dark() ? rgb(s, 22, 67, 75) : rgb(s, 208, 238, 238);
        fg = p.accent;
        break;
    case 1:
        tile = rgb(s, 24, 53, 71);
        fg = rgb(s, 131, 232, 218);
        break;
    case 2:
        tile = rgb(s, 52, 116, 174);
        fg = rgb(s, 224, 243, 255);
        break;
    case 3:
        tile = rgb(s, 54, 131, 148);
        fg = rgb(s, 231, 249, 250);
        break;
    case 4:
        tile = rgb(s, 70, 124, 180);
        fg = rgb(s, 235, 245, 255);
        break;
    case 5:
        tile = rgb(s, 114, 98, 173);
        fg = rgb(s, 243, 235, 255);
        break;
    case 6:
        tile = rgb(s, 171, 125, 56);
        fg = rgb(s, 255, 218, 145);
        break;
    default:
        tile = p.surface;
        fg = p.text;
        break;
    }
    Icon i = {s, box, tile, fg};
    rect(&i, 0, 0, 48, 48, 13, tile);
    rect(&i, 1, 1, 46, 17, 12, gui_blend(tile, rgb(s, 255, 255, 255), 24));
    // Keep one consistent 48-unit grid and stroke weight across every symbol.
    if (id == 0) {
        stroke(&i, 13, 12, 24, 24, 3, fg);
        stroke(&i, 35, 12, 24, 24, 3, fg);
        stroke(&i, 24, 24, 24, 37, 3, fg);
    } else if (id == 1) {
        stroke(&i, 11, 15, 19, 23, 3, fg);
        stroke(&i, 19, 23, 11, 31, 3, fg);
        stroke(&i, 25, 32, 37, 32, 3, fg);
    } else if (id == 2) {
        int spokes[8][4] = {{24, 8, 24, 14},  {24, 34, 24, 40}, {8, 24, 14, 24},  {34, 24, 40, 24},
                            {12, 12, 17, 17}, {31, 31, 36, 36}, {12, 36, 17, 31}, {31, 17, 36, 12}};
        for (unsigned a = 0; a < 8; ++a)
            stroke(&i, spokes[a][0], spokes[a][1], spokes[a][2], spokes[a][3], 4, fg);
        rect(&i, 13, 13, 22, 22, 11, fg);
        rect(&i, 20, 20, 8, 8, 4, tile);
    } else if (id == 3) {
        rect(&i, 12, 8, 25, 33, 4, fg);
        for (int row = 0; row < 3; ++row)
            stroke(&i, 17, 16 + row * 6, 30, 16 + row * 6, 2, tile);
        stroke(&i, 29, 36, 39, 26, 4, rgb(s, 218, 183, 106));
        stroke(&i, 28, 38, 31, 36, 2, tile);
    } else if (id == 4) {
        rect(&i, 11, 11, 26, 26, 13, fg);
        rect(&i, 23, 17, 3, 3, 1, tile);
        stroke(&i, 24, 24, 24, 31, 3, tile);
    } else if (id == 5) {
        rect(&i, 12, 7, 25, 35, 5, fg);
        rect(&i, 16, 11, 17, 8, 2, tile);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                rect(&i, 16 + col * 6, 23 + row * 5, 4, 3, 1, tile);
    } else if (id == 6) {
        rect(&i, 8, 11, 15, 9, 3, fg);
        rect(&i, 7, 17, 34, 24, 5, fg);
        rect(&i, 9, 21, 30, 17, 4, rgb(s, 255, 233, 180));
    } else if (id >= SHELL_ICON_UP) {
        if (id == SHELL_ICON_UP) {
            stroke(&i, 24, 35, 24, 13, 3, fg);
            stroke(&i, 14, 23, 24, 13, 3, fg);
            stroke(&i, 24, 13, 34, 23, 3, fg);
        } else if (id == SHELL_ICON_NEW_FOLDER) {
            rect(&i, 7, 13, 15, 7, 3, fg);
            rect(&i, 7, 19, 32, 21, 4, fg);
            stroke(&i, 23, 24, 23, 34, 3, tile);
            stroke(&i, 18, 29, 28, 29, 3, tile);
        } else if (id == SHELL_ICON_NEW_NOTE || id == SHELL_ICON_DOCUMENT) {
            rect(&i, 12, 8, 25, 33, 4, fg);
            if (id == SHELL_ICON_NEW_NOTE) {
                stroke(&i, 24, 19, 24, 31, 3, tile);
                stroke(&i, 18, 25, 30, 25, 3, tile);
            } else
                for (int n = 0; n < 3; ++n)
                    stroke(&i, 18, 18 + n * 6, 30, 18 + n * 6, 2, tile);
        } else if (id == SHELL_ICON_TRASH) {
            rect(&i, 15, 17, 21, 22, 3, fg);
            stroke(&i, 12, 13, 38, 13, 3, fg);
            stroke(&i, 20, 9, 30, 9, 3, fg);
            stroke(&i, 21, 22, 21, 33, 2, tile);
            stroke(&i, 29, 22, 29, 33, 2, tile);
        } else if (id == SHELL_ICON_REFRESH) {
            rect(&i, 11, 11, 27, 27, 13, fg);
            rect(&i, 15, 15, 19, 19, 9, tile);
            rect(&i, 27, 8, 14, 15, 0, tile);
            stroke(&i, 35, 10, 35, 22, 3, fg);
            stroke(&i, 35, 22, 24, 22, 3, fg);
        } else {
            int a = id == SHELL_ICON_PREVIOUS ? 29 : 19, b = id == SHELL_ICON_PREVIOUS ? 18 : 30;
            stroke(&i, a, 12, b, 24, 3, fg);
            stroke(&i, b, 24, a, 36, 3, fg);
        }
    } else {
        rect(&i, 12, 12, 25, 25, 12, fg);
        rect(&i, 15, 15, 19, 19, 9, tile);
        if (id == 7) {
            rect(&i, 20, 8, 10, 13, 0, tile);
            stroke(&i, 25, 8, 25, 23, 3, fg);
        } else {
            rect(&i, 29, 10, 12, 12, 0, tile);
            stroke(&i, 35, 10, 35, 22, 3, fg);
            stroke(&i, 35, 22, 24, 22, 3, fg);
        }
    }
}
static void text(const BootInfo *s, int x, int y, const char *label, GuiFont font, uint32_t c) {
    gui_text(s, x, y, label, font, c);
}
static void clipped(const BootInfo *s, int x, int y, int width, const char *label, GuiFont font,
                    uint32_t c) {
    char value[96];
    unsigned n = 0;
    for (; label[n] && n + 1 < sizeof(value); ++n) {
        value[n] = label[n];
        value[n + 1] = 0;
        if (gui_text_width(value, font) > width) {
            value[n] = 0;
            break;
        }
    }
    value[n] = 0;
    text(s, x, y, value, font, c);
}
void shell_tooltip(const BootInfo *s, GuiRect anchor, const char *name) {
    ShellPalette p = shell_palette(s);
    int width = gui_text_width(name, GUI_SMALL) + 26;
    int left = anchor.x + (anchor.width - width) / 2;
    if (left < 8)
        left = 8;
    if (left + width > (int)s->width - 8)
        left = (int)s->width - 8 - width;
    GuiRect b = {left, anchor.y - 44, width, 32};
    gui_shadow(s, b, 8);
    gui_round_rect(s, b, 9, p.border, 255);
    gui_round_rect(s, (GuiRect){b.x + 1, b.y + 1, b.width - 2, b.height - 2}, 8, p.surface, 255);
    text(s, b.x + 13, b.y + 9, name, GUI_SMALL, p.text);
}
void shell_launcher_draw(const BootInfo *s, const ShellLauncher *state, GuiRect b) {
    ShellPalette p = shell_palette(s);
    int compact = b.height < 440;
    gui_shadow(s, b, 22);
    gui_round_rect(s, b, 22, p.border, 255);
    gui_round_rect(s, (GuiRect){b.x + 1, b.y + 1, b.width - 2, b.height - 2}, 21, p.background, 255);
    shell_icon(s, 0, (GuiRect){b.x + 24, b.y + (compact ? 13 : 22), 32, 32});
    text(s, b.x + 68, b.y + (compact ? 16 : 22), "YAAI", GUI_BODY, p.text);
    text(s, b.x + 68, b.y + (compact ? 37 : 46), "Your workspace", GUI_SMALL, p.muted);
    GuiRect search = search_rect(b);
    gui_round_rect(s, search, 10, p.surface, 255);
    gui_line(s, search.x + 10, search.y + search.height - 1, search.x + search.width - 10,
             search.y + search.height - 1, 1, p.accent);
    if (state->select_all && state->query[0]) {
        int width = gui_text_width(state->query, GUI_SMALL) + 6;
        if (width > search.width - 48)
            width = search.width - 48;
        gui_round_rect(s, (GuiRect){search.x + 12, search.y + 8, width, 24}, 4, p.hover, 255);
    }
    clipped(s, search.x + 15, search.y + 12, search.width - 50,
            state->query[0] ? state->query : "Search apps...", GUI_SMALL,
            state->query[0] ? p.text : p.muted);
    if (state->query[0])
        text(s, search.x + search.width - 26, search.y + 12, "x", GUI_SMALL, p.muted);
    text(s, b.x + 26, b.y + (compact ? 108 : 134), state->query[0] ? "Search results" : "Your apps",
         GUI_SMALL, p.muted);
    unsigned count = shell_launcher_count(state);
    if (!count) {
        text(s, b.x + 32, b.y + (compact ? 168 : 210), "No matching apps", GUI_BODY, p.text);
    }
    for (unsigned i = 0; i < count; ++i) {
        const ShellApp *app = shell_launcher_app(state, i);
        GuiRect tile = shell_launcher_tile(b, i);
        if (state->selected == (int)i)
            gui_round_rect(s, tile, 13, p.hover, 255);
        int icon_size = compact ? 34 : 44;
        shell_icon(s, app->id,
                   (GuiRect){tile.x + (tile.width - icon_size) / 2, tile.y + (compact ? 5 : 10), icon_size,
                             icon_size});
        int width = gui_text_width(app->name, GUI_SMALL);
        text(s, tile.x + (tile.width - width) / 2, tile.y + (compact ? 47 : 66), app->name, GUI_SMALL,
             p.text);
        if (!compact) {
            width = gui_text_width(app->hint, GUI_SMALL);
            text(s, tile.x + (tile.width - width) / 2, tile.y + 85, app->hint, GUI_SMALL, p.muted);
        }
    }
    if (!compact)
        text(s, b.x + 26, b.y + b.height - 98, "Type to search  /  Tab to navigate  /  Enter to open",
             GUI_SMALL, p.muted);
    gui_line(s, b.x + 24, b.y + b.height - 76, b.x + b.width - 24, b.y + b.height - 76, 1, p.border);
    const char *names[2] = {"Restart", "Shut down"};
    for (unsigned i = 0; i < 2; ++i) {
        GuiRect button = power_rect(b, (int)i);
        gui_round_rect(s, button, 10, state->selected == (int)(count + i) ? p.hover : p.surface, 255);
        shell_icon(s, i ? 7 : 8, (GuiRect){button.x + 10, button.y + 5, 32, 32});
        text(s, button.x + 54, button.y + 13, names[i], GUI_SMALL, p.text);
    }
}
