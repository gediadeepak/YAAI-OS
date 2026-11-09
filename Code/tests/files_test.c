#include "../files.h"
#include "../setup.h"
#define CHECK(value) do { if (!(value)) return __LINE__; } while (0)
static uint8_t medium[2048][512], buffer[FILE_BYTES], output[FILE_BYTES];
static unsigned writes, fail_write, fail_flush;
static int read_fail, torn;
static int read_sector(void *context, uint64_t sector, void *data)
{
    (void)context; if (read_fail || sector >= 2048) return 0;
    for (unsigned i = 0; i < 512; ++i) ((uint8_t *)data)[i] = medium[sector][i]; return 1;
}
static int write_sector(void *context, uint64_t sector, const void *data)
{
    (void)context; if (sector >= 2048) return 0;
    if (++writes == fail_write) {
        if (torn) for (unsigned i = 0; i < 24; ++i) medium[sector][i] = ((const uint8_t *)data)[i];
        return 0;
    }
    for (unsigned i = 0; i < 512; ++i) medium[sector][i] = ((const uint8_t *)data)[i]; return 1;
}
static int flush(void *context) { (void)context; if (fail_flush) { --fail_flush; return 0; } return 1; }
static void put32(uint8_t *p, uint32_t value) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8)); }
static void format(void)
{
    for (unsigned s = 0; s < 2048; ++s) for (unsigned i = 0; i < 512; ++i) medium[s][i] = 0;
    for (unsigned i = 0; i < 8; ++i) medium[0][i] = "YAAIFS01"[i];
    put32(medium[0] + 8,1); put32(medium[0] + 12,512); put32(medium[0] + 16,32);
    put32(medium[0] + 20,4096); put32(medium[0] + 24,2048);
    put32(medium[0] + 32,setup_crc32(medium[0],512));
    writes = fail_write = fail_flush = 0; read_fail = torn = 0;
}
int files_test(void)
{
    BlockDevice disk = {0,2048,1,read_sector,write_sector,flush};
    format(); CHECK(files_mount(&disk) == FILE_OK);
    unsigned size = sizeof(output), count = 0;
    FileEntry entries[FILE_COUNT];
    CHECK(files_list(entries,FILE_COUNT,&count) == FILE_OK && count == 0);
    CHECK(files_read("hello.txt",output,&size) == FILE_NOT_FOUND);
    CHECK(files_write("hello.txt","hello",5) == FILE_OK);
    size = sizeof(output); CHECK(files_read("HELLO.TXT",output,&size) == FILE_OK && size == 5 && output[0] == 'h');
    unsigned before = writes; CHECK(files_write("../secret","x",1) == FILE_INVALID && writes == before);
    CHECK(files_write("oversize.txt",buffer,FILE_BYTES + 1) == FILE_TOO_LARGE && writes == before);
    output[0] = 0xcc; size = 1; CHECK(files_read("hello.txt",output,&size) == FILE_TOO_LARGE && size == 5 && output[0] == 0xcc);
    CHECK(files_write("hello.txt","new text",8) == FILE_OK);
    medium[11][0] ^= 0x80; // Corrupt the latest payload; retain the preceding valid bank.
    size = sizeof(output); CHECK(files_read("hello.txt",output,&size) == FILE_OK && size == 5);
    CHECK(files_write("hello.txt","new text",8) == FILE_OK);
    writes = 0; fail_write = 9; torn = 1;
    CHECK(files_write("hello.txt","interrupted",11) == FILE_IO_ERROR);
    size = sizeof(output); CHECK(files_read("hello.txt",output,&size) == FILE_OK && size == 8 && output[0] == 'n');
    fail_write = 0; torn = 0; fail_flush = 1;
    CHECK(files_write("hello.txt","not committed",13) == FILE_IO_ERROR);
    size = sizeof(output); CHECK(files_read("hello.txt",output,&size) == FILE_OK && size == 8);
    CHECK(files_list(entries,FILE_COUNT,&count) == FILE_OK && count == 1 && entries[0].size == 8);
    read_fail = 1; CHECK(files_write("other.txt","x",1) == FILE_IO_ERROR); read_fail = 0;
    disk.writable = 0; CHECK(files_write("hello.txt","x",1) == FILE_READ_ONLY); disk.writable = 1;
    medium[0][32] ^= 1; before = writes;
    CHECK(files_mount(&disk) == FILE_CORRUPT && files_write("x.txt","x",1) == FILE_UNAVAILABLE && writes == before);
    format(); medium[0][0] = 'X'; before = writes;
    CHECK(files_mount(&disk) == FILE_UNAVAILABLE && files_write("x.txt","x",1) == FILE_UNAVAILABLE && writes == before);
    format(); CHECK(files_mount(&disk) == FILE_OK);
    for (unsigned i = 0; i < 32; ++i) {
        char name[] = "f00.txt"; name[1] = (char)('0' + i / 10); name[2] = (char)('0' + i % 10);
        CHECK(files_write(name,"x",1) == FILE_OK);
    }
    CHECK(files_write("extra.txt","x",1) == FILE_FULL);
    CHECK(files_list(entries,FILE_COUNT,&count) == FILE_OK && count == 32);
    format(); CHECK(files_mount(&disk) == FILE_OK);
    for (unsigned i = 0; i < 4096; ++i) buffer[i] = (uint8_t)(i * 13);
    CHECK(files_write("binary.dat",buffer,4096) == FILE_OK);
    size = sizeof(output); CHECK(files_read("binary.dat",output,&size) == FILE_OK && size == 4096);
    for (unsigned i = 0; i < 4096; ++i) CHECK(output[i] == buffer[i]);
    CHECK(files_write("empty.txt",0,0) == FILE_OK);
    size = sizeof(output); CHECK(files_read("empty.txt",output,&size) == FILE_OK && !size);
    files_mount(0);
    return 0;
}

