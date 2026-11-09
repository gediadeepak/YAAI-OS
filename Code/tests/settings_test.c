#include "../display_mode.h"
#include "../settings.h"
#include "../storage.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
typedef struct {
    unsigned size;
    unsigned char bytes[300];
} Record;
static Record records[6];
static int fail;
static unsigned slot(const char *key) {
    unsigned n = 0;
    while (key[n])
        ++n;
    return (key[0] == 'T' ? 2 : key[0] == 'D' && key[1] == 'i' ? 4 : 0) + (key[n - 1] == 'B');
}
static StoreResult read(void *context, const char *key, void *p, unsigned *n) {
    (void)context;
    Record *r = &records[slot(key)];
    if (!r->size)
        return STORE_NOT_FOUND;
    if (*n < r->size)
        return STORE_LIMIT;
    *n = r->size;
    for (unsigned i = 0; i < *n; ++i)
        ((unsigned char *)p)[i] = r->bytes[i];
    return STORE_OK;
}
static StoreResult write(void *context, const char *key, const void *p, unsigned n) {
    (void)context;
    if (fail)
        return STORE_ERROR;
    Record *r = &records[slot(key)];
    if (n > 300)
        return STORE_LIMIT;
    r->size = n;
    for (unsigned i = 0; i < n; ++i)
        r->bytes[i] = ((const unsigned char *)p)[i];
    return STORE_OK;
}
static GraphicsInfo modes[2] = {{0, 1024, 768, 1, {0}, 1024}, {0, 1920, 1080, 1, {0}, 1920}};
static unsigned frees;
static int reject;
static uint64_t query(GraphicsOutput *g, uint32_t i, uint64_t *n, GraphicsInfo **p) {
    (void)g;
    *p = &modes[i];
    *n = sizeof(**p);
    return 0;
}
static uint64_t set(GraphicsOutput *g, uint32_t i) {
    if (reject && i == 0)
        return 1;
    g->mode->mode = i;
    g->mode->info = &modes[i];
    return 0;
}
static uint64_t release(void *p) {
    (void)p;
    ++frees;
    return 0;
}
int settings_test(void) {
    for (unsigned i = 0; i < 6; ++i)
        records[i] = (Record){0};
    fail = 0;
    storage_attach((StoreBackend){0, read, write});
    BootInfo boot = {0};
    boot.setup.zone = 2;
    uint32_t legacy[4] = {1, 2, 75, 1};
    CHECK(storage_put("Desktop", legacy, sizeof(legacy)) == STORE_OK);
    settings_initialize(&boot);
    CHECK(settings_current()->wallpaper == 2 && settings_current()->volume == 75 &&
          !settings_current()->theme);
    settings_theme(1);
    settings_resolution(1);
    settings_initialize(&boot);
    CHECK(settings_current()->theme == 1 && settings_current()->resolution == 1 &&
          settings_current()->volume == 75);
    settings_theme(7);
    settings_resolution(9);
    CHECK(settings_current()->theme == 1 && settings_current()->resolution == 1);
    fail = 1;
    settings_resolution(2);
    CHECK(settings_current()->resolution == 1);
    fail = 0;
    settings_resolution(2);
    settings_initialize(&boot);
    CHECK(settings_current()->resolution == 2);
    GraphicsMode mode = {2, 1, &modes[1], sizeof(GraphicsInfo), 0, 0};
    GraphicsOutput graphics = {query, set, 0, &mode};
    BootServices services = {0};
    services.free_pool = release;
    settings_resolution(1);
    reject = 0;
    frees = 0;
    display_select_mode(&graphics, &services);
    CHECK(mode.mode == 0 && frees == 1);
    mode.mode = 1;
    mode.info = &modes[1];
    reject = 1;
    display_select_mode(&graphics, &services);
    CHECK(mode.mode == 1);
    reject = 0;
    settings_resolution(2); // Unsupported 1280 x 720 falls back to best usable mode.
    mode.mode = 0;
    mode.info = &modes[0];
    display_select_mode(&graphics, &services);
    CHECK(mode.mode == 1);
    uint32_t invalid[2] = {1, 99};
    CHECK(storage_put("Display", invalid, sizeof(invalid)) == STORE_OK);
    settings_initialize(&boot);
    CHECK(settings_current()->resolution == 0 && settings_current()->theme == 1);
    storage_attach((StoreBackend){0});
    settings_initialize(&boot);
    return 0;
}
