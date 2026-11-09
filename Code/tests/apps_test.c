#include "../apps.h"
#include "../calculator.h"
#include "../file_app.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static WindowManager manager;
static int equal(const char *a, const char *b) {
    unsigned i = 0;
    while (a[i] && a[i] == b[i])
        ++i;
    return a[i] == b[i];
}
static void keys(Calculator *c, const char *s) {
    while (*s)
        calculator_key(c, *s++);
}
int apps_test(void) {
    Calculator c;
    calculator_reset(&c);
    keys(&c, "12.5*2=");
    CHECK(equal(c.text, "25"));
    keys(&c, "c1/3=");
    CHECK(equal(c.text, "0.333333"));
    keys(&c, "c7/0=");
    CHECK(c.error);
    calculator_key(&c, '9');
    CHECK(!c.error && equal(c.text, "9"));
    keys(&c, "c2n*3=");
    CHECK(equal(c.text, "-6"));
    keys(&c, "c2+3*4=");
    CHECK(equal(c.text, "20"));
    keys(&c, "c9223372036854.775807+0.000001=");
    CHECK(c.error);
    keys(&c, "c0.000001*0.000001=");
    CHECK(equal(c.text, "0"));
    keys(&c, "c12\b");
    CHECK(equal(c.text, "1"));
    windows_initialize(&manager);
    GuiRect rect = {0, 0, 600, 500};
    CHECK(!windows_register(&manager, 0, rect));
    CHECK(windows_register(&manager, APP_NOTES, rect));
    CHECK(windows_register(&manager, APP_CALCULATOR, rect));
    CHECK(!windows_register(&manager, APP_NOTES, rect));
    CHECK(!windows_focus(&manager, WINDOW_LIMIT));
    apps_initialize(&manager);
    files_mount(0);
    CHECK(!apps_launch(7) && !apps_launch(0));
    CHECK(apps_launch(APP_NOTES) && manager.active == APP_NOTES);
    apps_key(APP_NOTES, 'x');
    CHECK(file_app_has_changes());
    CHECK(apps_launch(APP_CALCULATOR) && manager.active == APP_CALCULATOR && manager.visible[APP_NOTES]);
    CHECK(!apps_close(APP_NOTES) && manager.visible[APP_NOTES]);
    apps_key(APP_CALCULATOR, '2');
    apps_key(APP_CALCULATOR, '+');
    apps_key(APP_CALCULATOR, '3');
    apps_key(APP_CALCULATOR, '=');
    CHECK(file_app_has_changes());
    windows_hide(&manager, APP_CALCULATOR);
    CHECK(manager.active == APP_NOTES);
    CHECK(apps_launch(APP_CALCULATOR));
    CHECK(apps_close(APP_CALCULATOR) && manager.active == APP_NOTES);
    apps_key(APP_NOTES, 7);
    apps_key(APP_NOTES, 7);
    CHECK(!file_app_has_changes());
    CHECK(apps_close(APP_NOTES) && manager.active == 0 && !manager.visible[APP_NOTES]);
    apps_initialize(0);
    return 0;
}
