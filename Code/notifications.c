#include "notifications.h"
#include "shell_ui.h"

enum { HISTORY_LIMIT = 6, TOAST_FRAMES = 160 };
typedef struct { char title[40], detail[112]; } Notification;
static Notification history[HISTORY_LIMIT];
static unsigned next, count, unread, toast_frames;
static int changed;

static void copy_text(char *out, unsigned capacity, const char *text)
{
    unsigned i = 0;
    while (i + 1 < capacity && text[i]) { out[i] = text[i]; ++i; }
    out[i] = 0;
}

void notification_post(const char *title, const char *detail)
{
    copy_text(history[next].title, sizeof(history[next].title), title);
    copy_text(history[next].detail, sizeof(history[next].detail), detail);
    next = (next + 1) % HISTORY_LIMIT;
    if (count < HISTORY_LIMIT) ++count;
    if (unread < HISTORY_LIMIT) ++unread;
    toast_frames = TOAST_FRAMES;
    changed = 1;
}

unsigned notification_unread(void) { return unread; }
void notification_mark_read(void) { unread = toast_frames = 0; }

int notification_tick(void)
{
    int redraw = changed;
    changed = 0;
    if (toast_frames && --toast_frames == 0) redraw = 1;
    return redraw;
}

static void detail_text(const BootInfo *screen, GuiRect row, int text_y, const char *detail, uint32_t color)
{
    char shown[112] = {0};
    for (unsigned i = 0; detail[i] && i + 1 < sizeof(shown); ++i) {
        shown[i] = detail[i];
        if (gui_text_width(shown, GUI_SMALL) > row.width - 28) { shown[i] = 0; break; }
    }
    gui_text(screen, row.x + 14, text_y, shown, GUI_SMALL, color);
}

static void draw_entry(const BootInfo *screen, GuiRect row, const Notification *item, ShellPalette palette)
{
    gui_round_rect(screen, row, 12, palette.surface, 255);
    int compact = row.height < 50;
    gui_text(screen, row.x + 14, row.y + (compact ? 2 : 9), item->title,
             compact ? GUI_SMALL : GUI_BODY, palette.text);
    detail_text(screen, row, row.y + (compact ? 15 : 31), item->detail, palette.muted);
}

void notification_clear(void)
{
    next = count = 0;
    notification_mark_read();
    changed = 1;
}

GuiRect notification_clear_button(GuiRect bounds)
{
    if (!count) return (GuiRect){0};
    return (GuiRect){bounds.x + bounds.width - 82, bounds.y + 9, 70, 32};
}

void notification_history_draw(const BootInfo *screen, GuiRect bounds, int clear_hovered)
{
    ShellPalette palette = shell_palette(screen);
    gui_shadow(screen, bounds, 18);
    gui_round_rect(screen, bounds, 18, palette.background, 255);
    gui_text(screen, bounds.x + 16, bounds.y + 14, "Notifications", GUI_BODY, palette.text);
    if (!count) {
        gui_text(screen, bounds.x + 16, bounds.y + 57, "No notifications", GUI_SMALL, palette.muted);
        return;
    }
    GuiRect clear = notification_clear_button(bounds);
    gui_round_rect(screen, clear, 8, clear_hovered ? palette.hover : palette.surface, 255);
    gui_text(screen, clear.x + 9, clear.y + 8, "Clear all", GUI_SMALL, palette.text);
    int row_height = (bounds.height - 55) / HISTORY_LIMIT;
    if (row_height > 58) row_height = 58;
    for (unsigned i = 0; i < count; ++i) {
        GuiRect row = {bounds.x + 10, bounds.y + 46 + (int)i * row_height,
                       bounds.width - 20, row_height - 3};
        draw_entry(screen, row, &history[(next + HISTORY_LIMIT - 1 - i) % HISTORY_LIMIT], palette);
    }
}

void notification_toast_draw(const BootInfo *screen, GuiRect bounds)
{
    if (!toast_frames) return;
    gui_shadow(screen, bounds, 12);
    draw_entry(screen, bounds, &history[(next + HISTORY_LIMIT - 1) % HISTORY_LIMIT], shell_palette(screen));
}
