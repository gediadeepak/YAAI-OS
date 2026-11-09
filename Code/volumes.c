#include "volumes.h"
#include "files.h"
#include "setup.h"
const uint8_t documents_partition_type[16] = {0x7e,0x49,0xa3,0x63,0x1d,0x1a,0x1b,0x49,0xa7,0x11,0x59,0x41,0x41,0x49,0x46,0x53};
static uint8_t entries[16384];
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }
static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static int equal(const uint8_t *a, const uint8_t *b, unsigned n) { for (unsigned i = 0; i < n; ++i) if (a[i] != b[i]) return 0; return 1; }
static int empty(const uint8_t *p) { unsigned v = 0; for (unsigned i = 0; i < 16; ++i) v |= p[i]; return !v; }
void documents_header(uint8_t header[512], uint64_t sectors, const uint8_t id[16])
{
    for (unsigned i = 0; i < 512; ++i) header[i] = 0;
    int modern=sectors>=FILE_V2_SECTORS;
    for (unsigned i = 0; i < 8; ++i) header[i] = (modern?"YAAIFS02":"YAAIFS01")[i];
    put32(header + 8,modern?2:1); put32(header + 12,512); put32(header + 16,modern?FILE_COUNT:32); put32(header + 20,modern?FILE_BYTES:4096);
    put32(header + 24,(uint32_t)sectors); put32(header + 28,(uint32_t)(sectors >> 32));
    for (unsigned i = 0; i < 16; ++i) header[40 + i] = id[i];
    put32(header + 32,setup_crc32(header,512));
}
static int read_sector(void *context, uint64_t lba, void *bytes)
{
    VolumeSlice *s = context;
    return lba < s->length && s->parent->read(s->parent->context,s->first + lba,bytes);
}
static int write_sector(void *context, uint64_t lba, const void *bytes)
{
    VolumeSlice *s = context;
    return s->parent->writable && s->parent->write && lba < s->length && s->parent->write(s->parent->context,s->first + lba,bytes);
}
static int flush(void *context) { VolumeSlice *s = context; return s->parent->flush && s->parent->flush(s->parent->context); }
int documents_partition(const BlockDevice *parent, VolumeSlice *slice, BlockDevice *device)
{
    uint8_t header[512];
    if (!parent || !parent->read || parent->sectors < 68 || !parent->read(parent->context,1,header)) return 0;
    if (!equal(header,(const uint8_t *)"EFI PART",8)) return 0;
    uint32_t size = get32(header + 12), expected = get32(header + 16);
    if (size < 92 || size > 512) return -1;
    put32(header + 16,0);
    if (get32(header + 8) != 0x10000 || get32(header + 20) || setup_crc32(header,size) != expected ||
        get64(header + 24) != 1 || get64(header + 32) != parent->sectors - 1 ||
        get64(header + 72) != 2 || get32(header + 80) != 128 || get32(header + 84) != 128) return -1;
    uint64_t first = get64(header + 40), last = get64(header + 48);
    if (first < 34 || first > last || last > parent->sectors - 34) return -1;
    for (unsigned i = 0; i < 32; ++i) if (!parent->read(parent->context,2 + i,entries + i * 512)) return -1;
    if (setup_crc32(entries,sizeof(entries)) != get32(header + 88)) return -1;
    int found = -1;
    for (unsigned i = 0; i < 128; ++i) {
        const uint8_t *p = entries + i * 128;
        if (empty(p)) continue;
        uint64_t start = get64(p + 32), end = get64(p + 40);
        if (start < first || start > end || end > last || empty(p + 16)) return -1;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *q = entries + j * 128;
            if (!empty(q) && (equal(p + 16,q + 16,16) || (start <= get64(q + 40) && get64(q + 32) <= end))) return -1;
        }
        if (equal(p,documents_partition_type,16)) {
            if (found >= 0 || get64(p + 48) || end - start + 1 < 577) return -1;
            found = (int)i;
        }
    }
    if (found < 0) return 0;
    const uint8_t *p = entries + found * 128;
    *slice = (VolumeSlice){parent,get64(p + 32),get64(p + 40) - get64(p + 32) + 1};
    *device = (BlockDevice){slice,slice->length,parent->writable,read_sector,write_sector,flush};
    return 1;
}
