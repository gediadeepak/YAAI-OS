#include "../files.h"
#include "../setup.h"
#include "../volumes.h"
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static uint8_t sectors[1024][512], payload[FILE_BYTES], out[FILE_BYTES];
static uint64_t addresses[1024];
static unsigned used, writes, fail_write, legacy_writes, flushes, fail_flush;
static int read_sector(void *c, uint64_t lba, void *p) {
    (void)c;
    if (lba >= 32768)
        return 0;
    unsigned index = 0;
    for (; index < used && addresses[index] != lba; ++index) {
    }
    for (unsigned i = 0; i < 512; ++i)
        ((uint8_t *)p)[i] = index < used ? sectors[index][i] : 0;
    return 1;
}
static int write_sector(void *c, uint64_t lba, const void *p) {
    (void)c;
    if (lba >= 32768 || ++writes == fail_write)
        return 0;
    if (lba < 1024)
        ++legacy_writes;
    unsigned index = 0;
    for (; index < used && addresses[index] != lba; ++index) {
    }
    if (index == used) {
        if (used == 1024)
            return 0;
        addresses[used++] = lba;
    }
    for (unsigned i = 0; i < 512; ++i)
        sectors[index][i] = ((const uint8_t *)p)[i];
    return 1;
}
static int flush(void *c) {
    (void)c;
    return ++flushes != fail_flush;
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (i * 8));
}
static void fixture(int legacy) {
    used = writes = fail_write = legacy_writes = flushes = fail_flush = 0;
    uint8_t h[512] = {0};
    if (legacy) {
        for (unsigned i = 0; i < 8; ++i)
            h[i] = "YAAIFS01"[i];
        put32(h + 8, 1);
        put32(h + 12, 512);
        put32(h + 16, 32);
        put32(h + 20, 4096);
        put32(h + 24, 32768);
        put32(h + 32, setup_crc32(h, 512));
    } else {
        uint8_t id[16] = {1};
        documents_header(h, 32768, id);
    }
    write_sector(0, 0, h);
    if (!legacy)
        write_sector(0, 1, h);
}
int folders_test(void) {
    BlockDevice disk = {0, 32768, 1, read_sector, write_sector, flush};
    FileEntry entries[FILE_COUNT];
    unsigned count, size, id, folder, child;
    FileUsage usage;
    char path[FILE_PATH];
    fixture(1);
    CHECK(files_mount(&disk) == FILE_OK && files_write("old.txt", "old data", 8) == FILE_OK);
    unsigned before = legacy_writes;
    CHECK(files_mkdir(0, "projects") == FILE_OK && legacy_writes == before);
    CHECK(files_usage(&usage) == FILE_OK && usage.version == 2 && usage.max_bytes == 65536);
    size = sizeof(out);
    CHECK(files_read("old.txt", out, &size) == FILE_OK && size == 8 && out[0] == 'o');
    CHECK(files_resolve("/projects", &folder) == FILE_OK);
    for (unsigned i = 0; i < FILE_BYTES; ++i)
        payload[i] = (uint8_t)(i * 17);
    CHECK(files_write_at(folder, "large.bin", payload, sizeof(payload)) == FILE_OK);
    CHECK(files_mount(&disk) == FILE_OK);
    size = sizeof(out);
    CHECK(files_read("/projects/large.bin", out, &size) == FILE_OK && size == FILE_BYTES);
    for (unsigned i = 0; i < FILE_BYTES; ++i)
        CHECK(out[i] == payload[i]);
    CHECK(files_resolve("projects/large.bin", &id) == FILE_OK);
    CHECK(files_mkdir(folder, "child") == FILE_OK && files_resolve("projects/child", &child) == FILE_OK);
    CHECK(files_move(folder, child) == FILE_BAD_MOVE);
    CHECK(files_rename(folder, "work") == FILE_OK && files_path(id, path, sizeof(path)) == FILE_OK);
    CHECK(path[1] == 'w' && files_resolve("work/large.bin", &id) == FILE_OK);
    CHECK(files_list_directory(folder, 0, entries, FILE_COUNT, &count) == FILE_OK && count == 2);
    CHECK(files_move(id, child) == FILE_OK && files_resolve("work/child/large.bin", &id) == FILE_OK);
    CHECK(files_write_at(child, "other.bin", "x", 1) == FILE_OK &&
          files_rename(id, "other.bin") == FILE_EXISTS);
    CHECK(files_trash(folder) == FILE_OK && files_resolve("work/child/large.bin", &id) == FILE_NOT_FOUND);
    CHECK(files_list_directory(0, 1, entries, FILE_COUNT, &count) == FILE_OK && count == 1 &&
          entries[0].id == folder);
    CHECK(files_remove(folder) == FILE_NOT_EMPTY && files_restore(folder) == FILE_OK);
    CHECK(files_resolve("work/child/large.bin", &id) == FILE_OK);
    CHECK(files_trash(id) == FILE_OK && files_restore(id) == FILE_OK);
    CHECK(files_trash(id) == FILE_OK && files_write_at(child, "large.bin", "new", 3) == FILE_OK);
    CHECK(files_restore(id) == FILE_EXISTS && files_remove(id) == FILE_OK);
    CHECK(files_entry(id, &entries[0]) == FILE_NOT_FOUND);
    CHECK(files_move(folder, folder) == FILE_BAD_MOVE);
    CHECK(files_write("../bad", "x", 1) == FILE_INVALID);
    CHECK(files_mount(&disk) == FILE_OK && files_resolve("work/child/large.bin", &id) == FILE_OK);
    disk.writable = 0;
    before = writes;
    CHECK(files_trash(folder) == FILE_READ_ONLY && writes == before);
    disk.writable = 1;
    fixture(1);
    CHECK(files_mount(&disk) == FILE_OK && files_write("keep.txt", "keep", 4) == FILE_OK);
    fail_write = writes + 5;
    CHECK(files_upgrade() == FILE_IO_ERROR);
    fail_write = 0;
    CHECK(files_mount(&disk) == FILE_OK);
    size = sizeof(out);
    CHECK(files_read("keep.txt", out, &size) == FILE_OK && size == 4);
    CHECK(files_upgrade() == FILE_OK);
    size = sizeof(out);
    CHECK(files_read("keep.txt", out, &size) == FILE_OK && size == 4);
    fixture(1);
    CHECK(files_mount(&disk) == FILE_OK && files_write("keep.txt", "keep", 4) == FILE_OK);
    fail_write = writes + 132;
    CHECK(files_upgrade() == FILE_IO_ERROR);
    fail_write = 0;
    CHECK(files_mount(&disk) == FILE_OK && files_usage(&usage) == FILE_OK && usage.version == 2);
    size = sizeof(out);
    CHECK(files_read("keep.txt", out, &size) == FILE_OK && size == 4);
    fixture(1);
    uint8_t damaged[512] = {0};
    for (unsigned i = 0; i < 8; ++i)
        damaged[i] = "YAAIFS02"[i];
    write_sector(0, 1024, damaged);
    CHECK(files_mount(&disk) == FILE_OK && files_upgrade() == FILE_CORRUPT);
    CHECK(files_write("x", "x", 1) == FILE_READ_ONLY);
    fixture(1);
    read_sector(0, 0, damaged);
    put32(damaged + 24, 2048);
    put32(damaged + 32, 0);
    put32(damaged + 32, setup_crc32(damaged, 512));
    write_sector(0, 0, damaged);
    CHECK(files_mount(&disk) == FILE_OK);
    before = writes;
    CHECK(files_upgrade() == FILE_FULL && writes == before);
    fixture(0);
    CHECK(files_mount(&disk) == FILE_OK && files_mkdir(0, "dir") == FILE_OK);
    CHECK(files_resolve("dir", &folder) == FILE_OK);
    fail_write = writes + 1;
    CHECK(files_rename(folder, "changed") == FILE_IO_ERROR);
    fail_write = 0;
    CHECK(files_mount(&disk) == FILE_OK && files_resolve("dir", &folder) == FILE_OK);
    uint8_t h[512];
    read_sector(0, 0, h);
    h[32] ^= 1;
    write_sector(0, 0, h);
    CHECK(files_mount(&disk) == FILE_OK); // Redundant root survives a damaged primary.
    files_mount(0);
    return 0;
}
