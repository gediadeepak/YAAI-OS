#ifndef YAAI_SHELL_UI_H
#define YAAI_SHELL_UI_H
#include "gui.h"
enum {
    SHELL_ICON_UP = 9,
    SHELL_ICON_NEW_FOLDER,
    SHELL_ICON_NEW_NOTE,
    SHELL_ICON_TRASH,
    SHELL_ICON_REFRESH,
    SHELL_ICON_PREVIOUS,
    SHELL_ICON_NEXT,
    SHELL_ICON_DOCUMENT
};
enum {
    SHELL_APP_COUNT = 6,
    SHELL_QUERY = 48,
    SHELL_CLOSE = -1,
    SHELL_SEARCH = 40,
    SHELL_CLEAR = 41,
    SHELL_DEVICES = 42
};

typedef struct {
    unsigned id;
    int target;
    const char *name, *hint, *keywords;
} ShellApp;

typedef struct {
    char query[SHELL_QUERY];
    int selected, select_all;
} ShellLauncher;

typedef struct {
    uint32_t background, border, surface, hover, text, muted, accent;
} ShellPalette;
const ShellApp *shell_app(unsigned index);
ShellPalette shell_palette(const BootInfo *screen);
void shell_icon(const BootInfo *screen, unsigned id, GuiRect box);
void shell_tooltip(const BootInfo *screen, GuiRect anchor, const char *name);
void shell_launcher_init(ShellLauncher *state);
unsigned shell_launcher_count(const ShellLauncher *state);
const ShellApp *shell_launcher_app(const ShellLauncher *state, unsigned index);
int shell_launcher_key(ShellLauncher *state, char key);
GuiRect shell_launcher_bounds(unsigned width, unsigned height, GuiRect dock);
GuiRect shell_launcher_tile(GuiRect bounds, unsigned index);
int shell_launcher_hit(const ShellLauncher *state, GuiRect bounds, int x, int y);
int shell_launcher_hover(ShellLauncher *state, GuiRect bounds, int x, int y);
void shell_launcher_draw(const BootInfo *screen, const ShellLauncher *state, GuiRect bounds);
#endif
