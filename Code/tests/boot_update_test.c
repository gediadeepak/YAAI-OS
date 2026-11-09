#include "../boot_update.h"
#include "../setup.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
typedef struct {
    uint8_t bytes[1024];
    uint64_t size;
} TestFile;
static TestFile files[BOOT_FILE_COUNT], saved[BOOT_FILE_COUNT];
static uint8_t old[512], a[512], b[512];
static unsigned writes, fail;
static void copy(void *to, const void *from, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        ((uint8_t *)to)[i] = ((const uint8_t *)from)[i];
}
static void image(uint8_t *p, uint8_t tag) {
    for (unsigned i = 0; i < 512; ++i)
        p[i] = 0;
    p[0] = 'M';
    p[1] = 'Z';
    p[60] = 128;
    p[128] = 'P';
    p[129] = 'E';
    p[132] = 0x64;
    p[133] = 0x86;
    p[152] = 0x0b;
    p[153] = 2;
    p[220] = 10;
    p[300] = tag;
}
static int read_file(void *c, unsigned key, uint8_t **p, uint64_t *size) {
    (void)c;
    if (key >= BOOT_FILE_COUNT || !files[key].size)
        return 0;
    *p = files[key].bytes;
    *size = files[key].size;
    return 1;
}
static void release(void *c, uint8_t *p) {
    (void)c;
    (void)p;
}
static int write_file(void *c, unsigned key, const void *p, uint64_t size) {
    (void)c;
    if (key >= BOOT_FILE_COUNT || size > 1024)
        return 0;
    ++writes;
    if (writes == fail) {
        copy(files[key].bytes, p, (unsigned)size / 2);
        files[key].size = size;
        return 0;
    }
    copy(files[key].bytes, p, (unsigned)size);
    files[key].size = size;
    return 1;
}
static void reset(void) {
    for (unsigned i = 0; i < BOOT_FILE_COUNT; ++i)
        files[i] = (TestFile){0};
    copy(files[BOOT_MAIN].bytes, old, 512);
    files[BOOT_MAIN].size = 512;
    writes = fail = 0;
}
static int selected(BootStore *store, unsigned crc) {
    BootRecord r, best = {0};
    int found = 0;
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t *p = 0;
        if (boot_store_record(store, BOOT_RECORD0 + i, &r) && r.slot < 2 && boot_store_image(store, &r, &p) &&
            (!found || r.generation > best.generation)) {
            best = r;
            found = 1;
        }
    }
    return found && best.crc == crc;
}
int boot_update_test(void) {
    image(old, 1);
    image(a, 2);
    image(b, 3);
    BootStore store = {0, read_file, release, write_file};
    unsigned ca = setup_crc32(a, 512), cb = setup_crc32(b, 512);
    reset();
    CHECK(boot_store_update(&store, 0, 0, 1) == UPDATE_NO_PREVIOUS && !writes);
    CHECK(boot_store_update(&store, a, 512, 0) == UPDATE_OK && selected(&store, ca));
    CHECK(files[BOOT_RECOVERY].size == 512 && files[BOOT_RECOVERY].bytes[300] == 1);
    unsigned before = writes;
    CHECK(boot_store_update(&store, a, 512, 0) == UPDATE_UNCHANGED && writes == before);
    for (unsigned i = 0; i < BOOT_FILE_COUNT; ++i)
        saved[i] = files[i];
    for (unsigned failure = 1; failure <= 2; ++failure) {
        for (unsigned i = 0; i < BOOT_FILE_COUNT; ++i)
            files[i] = saved[i];
        writes = 0;
        fail = failure;
        CHECK(boot_store_update(&store, b, 512, 0) == UPDATE_IO && selected(&store, ca));
        CHECK(files[BOOT_MAIN].bytes[300] == 2);
    }
    for (unsigned i = 0; i < BOOT_FILE_COUNT; ++i)
        files[i] = saved[i];
    writes = fail = 0;
    CHECK(boot_store_update(&store, b, 512, 0) == UPDATE_OK && selected(&store, cb));
    CHECK(boot_store_update(&store, 0, 0, 1) == UPDATE_OK && selected(&store, ca));
    CHECK(boot_store_update(&store, 0, 0, 1) == UPDATE_OK && selected(&store, cb));
    files[BOOT_RECORD1].bytes[60] ^= 1;
    CHECK(selected(&store, ca));
    for (unsigned failure = 1; failure <= 6; ++failure) {
        reset();
        fail = failure;
        UpdateResult result = boot_store_update(&store, a, 512, 0);
        CHECK(result == UPDATE_IO);
        if (files[BOOT_MAIN].bytes[300] != 1) {
            BootRecord recovery;
            CHECK(boot_store_record(&store, BOOT_RECOVERY_RECORD, &recovery));
            uint8_t *p = 0;
            CHECK(boot_store_image(&store, &recovery, &p) && p[300] == 1);
        }
    }
    reset();
    a[132] = 0x64;
    a[133] = 0xaa;
    CHECK(!boot_image_valid(a, 512) && boot_store_update(&store, a, 512, 0) == UPDATE_INVALID && !writes);
    image(a, 2);
    uint8_t bytes[64];
    BootRecord record = {1, 512, 0, ca}, parsed;
    boot_record_encode(bytes, &record);
    CHECK(boot_record_decode(bytes, 64, &parsed) && parsed.crc == ca);
    bytes[33] = 1;
    CHECK(!boot_record_decode(bytes, 64, &parsed) && !boot_record_decode(bytes, 63, &parsed));
    return 0;
}
