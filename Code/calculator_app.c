#include "apps.h"
#include "calculator.h"
#include "screen.h"
static Calculator calculator;
static int initialized;
static const char *labels[] = {"AC", "+/-", "Back", "/", "7", "8", "9", "*", "4", "5",
                               "6",  "-",   "1",    "2", "3", "+", "0", ".", "=", "="};
static const char keys[] = {'c', 'n', '\b', '/', '7', '8', '9', '*', '4', '5',
                            '6', '-', '1',  '2', '3', '+', '0', '.', '=', '='};
static uint32_t rgb(const BootInfo *s, unsigned r, unsigned g, unsigned b) {
    return color(s, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static GuiRect cell(GuiRect w, unsigned i) {
    int gap = 8, width = (w.width - 48 - 3 * gap) / 4, height = (w.height - 190 - 4 * gap) / 5;
    return (GuiRect){w.x + 24 + (int)(i % 4) * (width + gap), w.y + 162 + (int)(i / 4) * (height + gap),
                     i == 18 ? width * 2 + gap : width, height};
}
static void open(const AppServices *services) {
    (void)services;
    if (!initialized) {
        calculator_reset(&calculator);
        initialized = 1;
    }
}
static void key(char k) { calculator_key(&calculator, k); }
static int close(void) {
    calculator_reset(&calculator);
    return 1;
}
static void draw(const BootInfo *s, GuiRect w) {
    GuiRect display = {w.x + 24, w.y + 66, w.width - 48, 76};
    gui_round_rect(s, display, 12, rgb(s, 8, 20, 33), 255);
    GuiFont font = calculator.error ? GUI_SMALL : GUI_HEADING;
    if (gui_text_width(calculator.text, font) > display.width - 24)
        font = GUI_BODY;
    gui_text(s, display.x + display.width - 12 - gui_text_width(calculator.text, font), display.y + 26,
             calculator.text, font, calculator.error ? rgb(s, 240, 163, 159) : rgb(s, 217, 239, 239));
    gui_text(s, display.x + 12, display.y + 7, "Decimal calculator", GUI_SMALL, rgb(s, 126, 162, 182));
    for (unsigned i = 0; i < 19; ++i) {
        GuiRect r = cell(w, i);
        int operation = i % 4 == 3 || i >= 18;
        gui_round_rect(s, r, 10, operation ? rgb(s, 80, 76, 53) : rgb(s, 30, 52, 70), 255);
        gui_text(s, r.x + (r.width - gui_text_width(labels[i], GUI_BODY)) / 2, r.y + (r.height - 23) / 2,
                 labels[i], GUI_BODY, operation ? rgb(s, 239, 218, 164) : rgb(s, 220, 235, 244));
    }
}
static void click(GuiRect w, int x, int y) {
    for (unsigned i = 0; i < 19; ++i) {
        GuiRect r = cell(w, i);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            key(keys[i]);
            return;
        }
    }
}
const AppDefinition calculator_application = {
    APP_CALCULATOR, "Calculator", 340, 420, open, draw, key, click, 0, 0, close};
