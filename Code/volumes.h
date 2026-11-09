#ifndef YAAI_VOLUMES_H
#define YAAI_VOLUMES_H
#include "block.h"
extern const uint8_t documents_partition_type[16];
typedef struct { const BlockDevice *parent; uint64_t first, length; } VolumeSlice;
// 1 = one validated owned partition, 0 = absent, -1 = invalid/ambiguous GPT.
int documents_partition(const BlockDevice *parent, VolumeSlice *slice, BlockDevice *device);
void documents_header(uint8_t header[512], uint64_t sectors, const uint8_t id[16]);
#endif
