#include "../storage.h"
#include "../settings.h"
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
typedef struct { uint8_t data[2][300]; unsigned size[2], writes; int fail, torn, read_failure; } FakeStore;
static unsigned slot(const char *key)
{
    unsigned i = 0; while (key[i]) ++i;
    return i && key[i - 1] == 'B';
}
static StoreResult read_fake(void *context, const char *key, void *bytes, unsigned *size)
{
    FakeStore *fake = context; unsigned at = slot(key);
    if (fake->read_failure) return STORE_ERROR;
    if (!fake->size[at]) return STORE_NOT_FOUND;
    if (*size < fake->size[at]) return STORE_CORRUPT;
    *size = fake->size[at];
    for (unsigned i = 0; i < *size; ++i) ((uint8_t *)bytes)[i] = fake->data[at][i];
    return STORE_OK;
}
static StoreResult write_fake(void *context, const char *key, const void *bytes, unsigned size)
{
    FakeStore *fake = context; unsigned at = slot(key);
    if (fake->fail || size > sizeof(fake->data[0])) return STORE_ERROR;
    ++fake->writes; fake->size[at] = size;
    for (unsigned i = 0; i < size / (fake->torn ? 2 : 1); ++i) fake->data[at][i] = ((const uint8_t *)bytes)[i];
    return STORE_OK;
}
int storage_test(void)
{
    static FakeStore fake;
    fake = (FakeStore){0};
    uint32_t value = 11, restored = 0; unsigned size = sizeof(restored);
    storage_attach((StoreBackend){&fake,read_fake,write_fake});
    CHECK(storage_get("Test",&restored,&size) == STORE_NOT_FOUND);
    CHECK(storage_put("Test",&value,sizeof(value)) == STORE_OK && fake.writes == 1);
    CHECK(storage_put("Test",&value,sizeof(value)) == STORE_OK && fake.writes == 1);
    value = 22; CHECK(storage_put("Test",&value,sizeof(value)) == STORE_OK);
    CHECK(storage_get("Test",&restored,&size) == STORE_OK && restored == 22);
    size = 1; CHECK(storage_get("Test",&restored,&size) == STORE_LIMIT && size == sizeof(value));
    fake.data[1][32] ^= 1; size = sizeof(restored);
    CHECK(storage_get("Test",&restored,&size) == STORE_OK && restored == 11);
    value = 33; CHECK(storage_put("Test",&value,sizeof(value)) == STORE_OK);
    fake.torn = 1; value = 44;
    CHECK(storage_put("Test",&value,sizeof(value)) == STORE_ERROR);
    size = sizeof(restored); CHECK(storage_get("Test",&restored,&size) == STORE_OK && restored == 33);
    fake.torn = 0; fake.fail = 1;
    CHECK(storage_put("Test",&value,sizeof(value)) == STORE_ERROR);
    CHECK(storage_get("Test",&restored,&size) == STORE_OK && restored == 33);
    fake.fail = 0; fake.read_failure = 1;
    CHECK(storage_put("Test",&value,sizeof(value)) == STORE_ERROR);
    fake.read_failure = 0;
    CHECK(storage_put("../Desktop",&value,sizeof(value)) == STORE_LIMIT);
    CHECK(storage_put("Test",&value,257) == STORE_LIMIT && storage_put("Test",&value,0) == STORE_LIMIT);
    for (unsigned i = 0; i < 32; ++i) { // Any damaged header must be rejected or fall back to the other slot.
        uint8_t previous = fake.data[1][i]; fake.data[1][i] ^= 0x80;
        size = sizeof(restored); CHECK(storage_get("Test",&restored,&size) != STORE_OK);
        fake.data[1][i] = previous;
    }
    fake = (FakeStore){0};
    BootInfo boot = {0}; boot.setup.zone = 2;
    settings_initialize(&boot);
    CHECK(settings_current()->wallpaper == 0 && settings_current()->volume == 100 && settings_current()->zone == 2);
    settings_wallpaper(1); settings_volume(50); settings_zone(1);
    settings_initialize(&boot);
    CHECK(settings_current()->wallpaper == 1 && settings_current()->volume == 50 && settings_current()->zone == 1);
    unsigned writes = fake.writes;
    settings_wallpaper(1); settings_volume(50); settings_zone(1);
    CHECK(fake.writes == writes);
    settings_wallpaper(9); settings_volume(101); settings_zone(99);
    CHECK(fake.writes == writes && settings_current()->wallpaper == 1);
    fake.fail = 1; settings_wallpaper(2);
    CHECK(settings_current()->wallpaper == 2);
    fake.fail = 0; settings_initialize(&boot); CHECK(settings_current()->wallpaper == 1);
    uint32_t invalid[4] = {1,8,101,99};
    CHECK(storage_put("Desktop",invalid,sizeof(invalid)) == STORE_OK);
    settings_initialize(&boot); CHECK(settings_current()->wallpaper == 0 && settings_current()->volume == 100);
    storage_attach((StoreBackend){0});
    CHECK(storage_put("Test",&value,sizeof(value)) == STORE_UNAVAILABLE);
    return 0;
}
