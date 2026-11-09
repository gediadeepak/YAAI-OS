#ifndef YAAI_BOOT_UPDATE_H
#define YAAI_BOOT_UPDATE_H
#include <stdint.h>
enum {
    BOOT_MAIN,
    BOOT_RECOVERY,
    BOOT_RECOVERY_RECORD,
    BOOT_SLOT0,
    BOOT_SLOT1,
    BOOT_RECORD0,
    BOOT_RECORD1,
    BOOT_MARKER,
    BOOT_FILE_COUNT
};
typedef struct {
    void *context;
    int (*read)(void *, unsigned, uint8_t **, uint64_t *);
    void (*release)(void *, uint8_t *);
    int (*write)(void *, unsigned, const void *, uint64_t);
} BootStore;
typedef struct {
    uint64_t generation, size;
    unsigned slot, crc;
} BootRecord;
typedef enum { UPDATE_OK, UPDATE_UNCHANGED, UPDATE_IO, UPDATE_INVALID, UPDATE_NO_PREVIOUS } UpdateResult;
int boot_record_decode(const uint8_t *bytes, uint64_t size, BootRecord *record);
void boot_record_encode(uint8_t bytes[64], const BootRecord *record);
int boot_image_valid(const void *bytes, uint64_t size);
int boot_store_record(const BootStore *store, unsigned key, BootRecord *record);
int boot_store_image(const BootStore *store, const BootRecord *record, uint8_t **bytes);
UpdateResult boot_store_update(const BootStore *store, const uint8_t *image, uint64_t size, int rollback);
const char *boot_update_message(UpdateResult result);
#endif
