#ifndef SETUP_H
#define SETUP_H
#include <stdint.h>

typedef struct {
    uint64_t signature;
    uint32_t version, zone;
    uint64_t ram_limit_mib;
    uint64_t disk_sectors, esp_sectors, data_sectors, allocated_sectors;
    uint32_t checksum, reserved;
} SetupConfig;

#define SETUP_SIGNATURE 0x3154455349524853ULL
#define SETUP_VERSION 1

const char *setup_zone_name(uint32_t zone);
uint32_t setup_crc32(const void *buffer, uint64_t size);
int setup_valid(const SetupConfig *config);
#endif
