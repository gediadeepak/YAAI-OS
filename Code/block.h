#ifndef YAAI_BLOCK_H
#define YAAI_BLOCK_H
#include <stdint.h>
typedef struct {
    void *context;
    uint64_t sectors;
    int writable;
    int (*read)(void *, uint64_t, void *);
    int (*write)(void *, uint64_t, const void *);
    int (*flush)(void *);
} BlockDevice;
void platform_disks_initialize(void);
unsigned platform_disk_count(void);
const BlockDevice *platform_disk(unsigned index);
#endif
