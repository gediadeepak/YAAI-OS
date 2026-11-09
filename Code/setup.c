#include <stddef.h>
#include "setup.h"

uint32_t setup_crc32(const void *buffer, uint64_t size)
{
    const uint8_t *bytes = buffer;
    uint32_t crc = 0xffffffffu;
    for (uint64_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

const char *setup_zone_name(uint32_t zone)
{
    static const char *const zones[] = {
        "UTC", "INDIA / IST", "UNITED KINGDOM / LONDON", "USA / NEW YORK",
        "USA / CHICAGO", "USA / DENVER", "USA / LOS ANGELES"
    };
    return zone < 7 ? zones[zone] : zones[0];
}

int setup_valid(const SetupConfig *config)
{
    return config->signature == SETUP_SIGNATURE && config->version == SETUP_VERSION &&
           config->zone < 7 && config->reserved == 0 &&
           (!config->ram_limit_mib || (config->ram_limit_mib >= 128 && config->ram_limit_mib <= 32768)) &&
           config->disk_sectors >= 524288 && config->disk_sectors <= 67108864 &&
           config->allocated_sectors >= 524288 && config->allocated_sectors <= config->disk_sectors &&
           config->esp_sectors == 262144 &&
           config->data_sectors == config->allocated_sectors - 34 - 264192 &&
           config->checksum == setup_crc32(config, offsetof(SetupConfig, checksum));
}
