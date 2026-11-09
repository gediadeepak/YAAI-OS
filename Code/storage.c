#include "storage.h"
#include "setup.h"
#include <stddef.h>
enum { MAX_DATA = 256, HEADER = 32 };
typedef struct {
    uint32_t magic, version, length, reserved;
    uint64_t generation;
    uint32_t crc, reserved2;
    uint8_t payload[MAX_DATA];
} Record;
_Static_assert(offsetof(Record,payload) == HEADER,"Settings record layout");
static StoreBackend backend;
void storage_attach(StoreBackend value) { backend = value; }
static int name(const char *key, char *result, unsigned slot)
{
    if (!key) return 0;
    unsigned i = 0;
    for (; key[i] && i < 32; ++i) {
        unsigned c = (uint8_t)key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
        result[i] = (char)c;
    }
    if (!i || key[i]) return 0;
    result[i++] = (char)('A' + slot); result[i] = 0; return 1;
}
static uint32_t checksum(Record *record)
{
    uint32_t saved = record->crc; record->crc = 0;
    uint32_t crc = setup_crc32(record,HEADER + record->length);
    record->crc = saved; return crc;
}
static StoreResult load(const char *key, unsigned slot, Record *record)
{
    char slot_name[34];
    if (!name(key,slot_name,slot)) return STORE_LIMIT;
    unsigned size = sizeof(*record);
    StoreResult result = backend.read(backend.context,slot_name,record,&size);
    if (result != STORE_OK) return result;
    if (size < HEADER || size > sizeof(*record) || record->magic != 0x53494159 || record->version != 1 ||
        !record->length || record->length > MAX_DATA || size != HEADER + record->length ||
        record->reserved || record->reserved2 || !record->generation || record->crc != checksum(record)) return STORE_CORRUPT;
    return STORE_OK;
}
static int select_record(Record *a, Record *b, StoreResult ra, StoreResult rb)
{
    if (ra != STORE_OK) return rb == STORE_OK ? 1 : -1;
    return rb == STORE_OK && b->generation > a->generation ? 1 : 0;
}
StoreResult storage_get(const char *key, void *bytes, unsigned *size)
{
    if (!backend.read) return STORE_UNAVAILABLE;
    if (!bytes || !size) return STORE_LIMIT;
    Record records[2];
    StoreResult a = load(key,0,&records[0]), b = load(key,1,&records[1]);
    int selected = select_record(&records[0],&records[1],a,b);
    if (selected < 0) return a != STORE_NOT_FOUND ? a : b;
    Record *record = &records[selected];
    unsigned capacity = *size; *size = record->length;
    if (capacity < record->length) return STORE_LIMIT;
    for (unsigned i = 0; i < record->length; ++i) ((uint8_t *)bytes)[i] = record->payload[i];
    return STORE_OK;
}
StoreResult storage_put(const char *key, const void *bytes, unsigned size)
{
    if (!backend.read || !backend.write) return STORE_UNAVAILABLE;
    char slot_name[34];
    if (!bytes || !size || size > MAX_DATA || !name(key,slot_name,0)) return STORE_LIMIT;
    Record records[2];
    StoreResult a = load(key,0,&records[0]), b = load(key,1,&records[1]);
    if ((a != STORE_OK && a != STORE_NOT_FOUND && a != STORE_CORRUPT) ||
        (b != STORE_OK && b != STORE_NOT_FOUND && b != STORE_CORRUPT)) return STORE_ERROR;
    int selected = select_record(&records[0],&records[1],a,b);
    uint64_t generation = selected < 0 ? 0 : records[selected].generation;
    if (selected >= 0 && records[selected].length == size) {
        unsigned i = 0;
        for (; i < size && records[selected].payload[i] == ((const uint8_t *)bytes)[i]; ++i) {}
        if (i == size) return STORE_OK; // Do not wear persistent storage for unchanged values.
    }
    if (generation == UINT64_MAX) return STORE_LIMIT;
    unsigned slot = selected < 0 ? 0 : (unsigned)(1 - selected);
    Record record = {0}; record.magic = 0x53494159; record.version = 1;
    record.length = size; record.generation = generation + 1;
    for (unsigned i = 0; i < size; ++i) record.payload[i] = ((const uint8_t *)bytes)[i];
    record.crc = checksum(&record); name(key,slot_name,slot);
    StoreResult result = backend.write(backend.context,slot_name,&record,HEADER + size);
    if (result != STORE_OK) return result;
    Record verify;
    if (load(key,slot,&verify) != STORE_OK || verify.generation != record.generation || verify.length != size) return STORE_ERROR;
    for (unsigned i = 0; i < size; ++i) if (verify.payload[i] != record.payload[i]) return STORE_ERROR;
    return STORE_OK;
}
