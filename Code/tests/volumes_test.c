#include "../volumes.h"
#include "../files.h"
#include "../power.h"
#include "../uefi.h"
#include "../setup.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint8_t media[2048][512];
static unsigned writes, resets, reset_type, fail_flush;
static int read_sector(void *c, uint64_t n, void *p) { (void)c; if (n >= 2048) return 0; for (unsigned i = 0; i < 512; ++i) ((uint8_t *)p)[i] = media[n][i]; return 1; }
static int write_sector(void *c, uint64_t n, const void *p) { (void)c; if (n >= 2048) return 0; ++writes; for (unsigned i = 0; i < 512; ++i) media[n][i] = ((const uint8_t *)p)[i]; return 1; }
static int flush(void *c) { (void)c; return !fail_flush; }
static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void checksums(void) { put32(media[1] + 88,setup_crc32(media[2],16384)); put32(media[1] + 16,0); put32(media[1] + 16,setup_crc32(media[1],92)); }
static void fixture(void)
{
    for (unsigned s = 0; s < 2048; ++s) for (unsigned i = 0; i < 512; ++i) media[s][i] = 0;
    for (unsigned i = 0; i < 8; ++i) media[1][i] = "EFI PART"[i];
    put32(media[1] + 8,0x10000); put32(media[1] + 12,92); put32(media[1] + 24,1); put32(media[1] + 32,2047);
    put32(media[1] + 40,34); put32(media[1] + 48,2014); put32(media[1] + 72,2); put32(media[1] + 80,128); put32(media[1] + 84,128);
    for (unsigned i = 0; i < 16; ++i) media[2][i] = documents_partition_type[i];
    media[2][16] = 1; put32(media[2] + 32,512); put32(media[2] + 40,1535);
    uint8_t id[16] = {1}; documents_header(media[512],1024,id);
    checksums(); writes = fail_flush = 0;
}
static void reset(uint32_t type, uint64_t status, uint64_t size, void *data) { if (!status && !size && !data) { ++resets; reset_type = type; } }
int volumes_test(void)
{
    BlockDevice parent = {0,2048,1,read_sector,write_sector,flush}, device;
    VolumeSlice slice;
    fixture(); CHECK(documents_partition(&parent,&slice,&device) == 1 && slice.first == 512 && device.sectors == 1024 && !writes);
    uint8_t block[512]; CHECK(!device.read(device.context,1024,block)); CHECK(!device.write(device.context,1024,block) && !writes);
    CHECK(files_mount(&device) == FILE_OK && files_write("one.txt","one disk",8) == FILE_OK);
    CHECK(media[512][0] == 'Y' && media[513][0] == 'Y' && media[0][0] == 0 && media[34][0] == 0 && media[1536][0] == 0);
    unsigned size = 512; CHECK(files_read("one.txt",block,&size) == FILE_OK && size == 8 && block[0] == 'o');
    CHECK(files_sync() == FILE_OK); fail_flush = 1; CHECK(files_sync() == FILE_IO_ERROR);
    struct { TableHeader header; UnusedService padding[10]; void (*reset)(uint32_t,uint64_t,uint64_t,void *); } runtime = {0};
    BootInfo boot = {0}; boot.runtime_services = &runtime;
    runtime.header.signature = 0x56524553544e5552ULL; runtime.header.header_size = sizeof(runtime); runtime.reset = reset;
    resets = 0; power_finish(&boot,1); CHECK(!resets); // A failed flush must prevent power loss.
    fail_flush = 0; power_finish(&boot,1); CHECK(resets == 1 && reset_type == 0);
    power_finish(&boot,0); CHECK(resets == 2 && reset_type == 2);
    runtime.header.signature = 0; power_finish(&boot,0); CHECK(resets == 2);
    fixture(); media[1][16] ^= 1; CHECK(documents_partition(&parent,&slice,&device) == -1 && !writes);
    fixture(); media[2][16] ^= 2; CHECK(documents_partition(&parent,&slice,&device) == -1 && !writes);
    fixture(); put32(media[2] + 40,2047); checksums(); CHECK(documents_partition(&parent,&slice,&device) == -1);
    fixture(); put32(media[2] + 32,1536); checksums(); CHECK(documents_partition(&parent,&slice,&device) == -1);
    fixture(); for (unsigned i = 0; i < 128; ++i) media[2][128 + i] = media[2][i]; media[2][144] = 2;
    checksums(); CHECK(documents_partition(&parent,&slice,&device) == -1); // overlap and duplicate owned volume
    fixture(); media[2][0] ^= 1; checksums(); CHECK(documents_partition(&parent,&slice,&device) == 0 && !writes);
    fixture(); parent.writable = 0; CHECK(documents_partition(&parent,&slice,&device) == 1 && !device.writable);
    CHECK(files_mount(&device) == FILE_OK && files_write("x","x",1) == FILE_READ_ONLY && !writes);
    files_mount(0); CHECK(files_sync() == FILE_OK);
    return 0;
}
