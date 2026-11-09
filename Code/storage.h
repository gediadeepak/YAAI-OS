#ifndef YAAI_STORAGE_H
#define YAAI_STORAGE_H
#include <stdint.h>
typedef enum { STORE_OK, STORE_NOT_FOUND, STORE_UNAVAILABLE, STORE_ERROR, STORE_CORRUPT, STORE_LIMIT } StoreResult;
typedef struct {
    void *context;
    StoreResult (*read)(void *, const char *, void *, unsigned *);
    StoreResult (*write)(void *, const char *, const void *, unsigned);
} StoreBackend;
// Bounded, checksummed settings records; this is not the document filesystem API.
void storage_attach(StoreBackend backend);
StoreResult storage_get(const char *key, void *bytes, unsigned *size);
StoreResult storage_put(const char *key, const void *bytes, unsigned size);
#endif
