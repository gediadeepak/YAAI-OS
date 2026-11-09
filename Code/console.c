#include "console.h"
#include "screen.h"
#include "gui.h"

enum { CELL_WIDTH = 12, MAX_COLUMNS = 160, MAX_ROWS = 80 };
static int cell_height = 18;
static const BootInfo *screen, *physical_screen;
static int left, top, view_left, view_top, view_width, view_height, windowed, cursor_visible;
static unsigned column, row, columns, rows;
static char cells[MAX_ROWS][MAX_COLUMNS];
static uint32_t foreground, background;

static void configure(const BootInfo *boot, int x, int y, int width, int height, int in_window)
{
    screen = boot;
    windowed = in_window;
    cell_height = in_window ? 24 : 18;
    view_left = x; view_top = y; view_width = width; view_height = height;
    left = in_window ? x + 12 : 24;
    top = in_window ? y + 12 : 76;
    int text_width = width - (in_window ? 24 : 48);
    int text_height = height - (in_window ? 24 : 100);
    columns = text_width >= CELL_WIDTH ? (unsigned)(text_width / CELL_WIDTH) : 1;
    rows = text_height >= cell_height ? (unsigned)(text_height / cell_height) : 1;
    if (columns > MAX_COLUMNS) columns = MAX_COLUMNS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    foreground = color(boot, 216, 231, 242);
    background = in_window ? color(boot,14,23,39) : color(boot,7,14,25);
    console_clear();
}

void console_initialize(const BootInfo *boot)
{
    physical_screen = boot;
    configure(boot, 0, 0, (int)boot->width, (int)boot->height, 0);
}

void console_restore_fullscreen(void)
{
    if (physical_screen) console_initialize(physical_screen);
}

void console_set_viewport(const BootInfo *boot, int x, int y, int width, int height)
{
    configure(boot, x, y, width, height, 1);
}

void console_reframe(int x, int y, int width, int height)
{
    view_left = x; view_top = y; view_width = width; view_height = height;
    left = x + 12; top = y + 12;
    columns = (unsigned)((width - 24) / CELL_WIDTH);
    rows = (unsigned)((height - 24) / cell_height);
    if (!columns) columns = 1;
    if (!rows) rows = 1;
    if (columns > MAX_COLUMNS) columns = MAX_COLUMNS;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (column >= columns) column = columns - 1;
    if (row >= rows) {
        unsigned skipped = row - rows + 1;
        for (unsigned line = 0; line < MAX_ROWS; ++line)
            for (unsigned cell = 0; cell < MAX_COLUMNS; ++cell)
                cells[line][cell] = line + skipped < MAX_ROWS ? cells[line + skipped][cell] : ' ';
        row = rows - 1;
    }
}

static void render_cell(unsigned x, unsigned y)
{
    if (windowed) gui_mono_character(screen,cells[y][x],left + x * CELL_WIDTH,top + y * cell_height,foreground,background);
    else screen_character(screen,cells[y][x],left + x * CELL_WIDTH,top + y * cell_height,2,foreground,background);
}

void console_redraw(void)
{
    for (int y = view_top; y < view_top + view_height; ++y) {
        for (int x = view_left; x < view_left + view_width; ++x) pixel(screen, x, y, background);
    }
    if (!windowed) {
        const char *title = "YAAI OS / COMMAND CONSOLE";
        for (unsigned index = 0; title[index]; ++index) {
            screen_character(screen, title[index], 24 + index * CELL_WIDTH, 24, 2,
                             color(screen, 90, 212, 225), background);
        }
    }
    for (unsigned y = 0; y < rows; ++y) {
        for (unsigned x = 0; x < columns; ++x) render_cell(x, y);
    }
    if (cursor_visible) console_cursor(1);
}

void console_clear(void)
{
    for (unsigned y = 0; y < MAX_ROWS; ++y) {
        for (unsigned x = 0; x < MAX_COLUMNS; ++x) cells[y][x] = ' ';
    }
    column = row = 0;
    cursor_visible = 0;
    console_redraw();
}

static void newline(void)
{
    column = 0;
    if (++row < rows) return;
    for (unsigned y = 0; y + 1 < rows; ++y) {
        for (unsigned x = 0; x < columns; ++x) cells[y][x] = cells[y + 1][x];
    }
    row = rows - 1;
    for (unsigned x = 0; x < columns; ++x) cells[row][x] = ' ';
    console_redraw();
}

void console_write(const char *text)
{
    serial_write(text);
    for (; *text; ++text) {
        if (*text == '\n') newline();
        else if (*text != '\r') {
            cells[row][column] = *text;
            render_cell(column, row);
            if (++column == columns) newline();
        }
    }
}

void console_backspace(void)
{
    if (column > 0) --column;
    else if (row > 0) { --row; column = columns - 1; }
    else return;
    cells[row][column] = ' ';
    render_cell(column, row);
    serial_write("\b \b");
}

void console_cursor(int visible)
{
    cursor_visible = visible;
    uint32_t fill = visible ? color(screen, 90, 212, 225) : background;
    for (unsigned x = 0; x < CELL_WIDTH - 2; ++x) {
        pixel(screen, left + column * CELL_WIDTH + x, top + row * cell_height + cell_height - 3, fill);
    }
}

void console_hex(uint64_t value)
{
    char text[19] = "0X";
    for (unsigned digit = 0; digit < 16; ++digit) {
        text[2 + digit] = "0123456789ABCDEF"[(value >> ((15 - digit) * 4)) & 15];
    }
    text[18] = 0;
    console_write(text);
}

unsigned console_columns(void) { return columns; }

void console_decimal(uint64_t value)
{
    char digits[21];
    unsigned position = sizeof(digits) - 1;
    digits[position] = 0;
    do { digits[--position] = (char)('0' + value % 10); value /= 10; } while (value);
    console_write(digits + position);
}
