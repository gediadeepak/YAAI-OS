#include <stddef.h>
#include "install_disk.h"
#include "volumes.h"
#include "files.h"

enum { SECTOR = 512, ESP_START = 2048, ESP_SECTORS = 262144 };
static _Alignas(4096) uint8_t io_buffer[65536];
static _Alignas(4096) uint8_t partition_entries[16384];
static uint8_t disk_guid[16];

static void zero(void *buffer, uint64_t count)
{
    uint8_t *bytes = buffer;
    for (uint64_t index = 0; index < count; ++index) bytes[index] = 0;
}

static void copy(void *destination, const void *source, uint64_t count)
{
    uint8_t *target = destination;
    const uint8_t *bytes = source;
    for (uint64_t index = 0; index < count; ++index) target[index] = bytes[index];
}

static void put16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *buffer, uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte) buffer[byte] = (uint8_t)(value >> (byte * 8));
}

static void put64(uint8_t *buffer, uint64_t value)
{
    for (unsigned byte = 0; byte < 8; ++byte) buffer[byte] = (uint8_t)(value >> (byte * 8));
}

static int read_blocks(BlockIO *disk, uint64_t lba, uint64_t sectors)
{
    return sectors <= sizeof(io_buffer) / SECTOR &&
           disk->read(disk, disk->media->id, lba, sectors * SECTOR, io_buffer) == 0;
}

static int write_blocks(BlockIO *disk, uint64_t lba, uint64_t sectors, const void *buffer)
{
    return disk->write(disk, disk->media->id, lba, sectors * SECTOR, buffer) == 0;
}

typedef struct {
    uint64_t signature, last_block;
    uint32_t image_crc, header_crc;
} PendingInstall;

#define PENDING_SIGNATURE 0x314c4154534e4953ULL

int install_disk_state(BlockIO *disk)
{
    if (!disk->media->present || disk->media->read_only || disk->media->logical_partition ||
        disk->media->block_size != SECTOR || disk->media->io_align > 4096) return 0;
    if (!read_blocks(disk, 0, 1)) return 0;
    const PendingInstall *pending = (const void *)io_buffer;
    if (pending->signature == PENDING_SIGNATURE && pending->last_block == disk->media->last_block &&
        pending->header_crc == setup_crc32(pending, offsetof(PendingInstall, header_crc))) return 2;
    // Inspect every sector. Missing partition signatures alone do not imply blank media.
    for (uint64_t lba = 0; lba <= disk->media->last_block;) {
        uint64_t count = disk->media->last_block - lba + 1;
        if (count > sizeof(io_buffer) / SECTOR) count = sizeof(io_buffer) / SECTOR;
        if (!read_blocks(disk, lba, count)) return 0;
        const uint64_t *words = (const void *)io_buffer;
        for (uint64_t word = 0; word < count * SECTOR / sizeof(uint64_t); ++word) {
            if (words[word]) return 0;
        }
        lba += count;
    }
    return 1;
}

typedef struct {
    uint32_t start, sectors, fat_sectors, clusters;
    uint8_t sectors_per_cluster;
} Volume;

static int volume_layout(Volume *volume, uint32_t start, uint32_t sectors)
{
    volume->start = start;
    volume->sectors = sectors;
    volume->sectors_per_cluster = 1;
    // Keep the FAT modest for larger data volumes while retaining FAT32 cluster counts.
    while (sectors / volume->sectors_per_cluster > 1048576 && volume->sectors_per_cluster < 64) {
        volume->sectors_per_cluster *= 2;
    }
    uint32_t fat = 1;
    for (;;) {
        uint32_t clusters = (sectors - 32 - 2 * fat) / volume->sectors_per_cluster;
        uint32_t required = (uint32_t)(((uint64_t)clusters + 2) * 4 + SECTOR - 1) / SECTOR;
        if (required <= fat) {
            volume->fat_sectors = fat;
            volume->clusters = clusters;
            return clusters >= 65525 && clusters < 0x0ffffff5;
        }
        fat = required;
    }
}

static uint64_t cluster_lba(const Volume *volume, uint32_t cluster)
{
    return (uint64_t)volume->start + 32 + volume->fat_sectors * 2 +
           (uint64_t)(cluster - 2) * volume->sectors_per_cluster;
}

static void directory_entry(uint8_t *entry, const char *name, uint8_t attributes,
                            uint32_t cluster, uint32_t size)
{
    copy(entry, name, 11);
    entry[11] = attributes;
    put16(entry + 20, (uint16_t)(cluster >> 16));
    put16(entry + 26, (uint16_t)cluster);
    put32(entry + 28, size);
    put16(entry + 16, (20 << 9) | (1 << 5) | 1); // Valid FAT creation date.
    put16(entry + 24, (20 << 9) | (1 << 5) | 1);
}

static int write_directory(BlockIO *disk, const Volume *volume, uint32_t cluster,
                            uint32_t image_size, uint32_t config_cluster, int esp)
{
    uint32_t bytes = volume->sectors_per_cluster * SECTOR;
    zero(io_buffer, bytes);
    if (!esp) {
        directory_entry(io_buffer, "YAAI DATA  ", 8, 0, 0);
    } else if (cluster == 2) {
        directory_entry(io_buffer, "EFI        ", 16, 3, 0);
    } else {
        directory_entry(io_buffer, ".          ", 16, cluster, 0);
        directory_entry(io_buffer + 32, "..         ", 16, cluster == 3 ? 0 : 3, 0);
        if (cluster == 3) {
            directory_entry(io_buffer + 64, "BOOT       ", 16, 4, 0);
            directory_entry(io_buffer + 96, "SHRI       ", 16, 5, 0);
        } else if (cluster == 4) {
            directory_entry(io_buffer + 64, "BOOTX64 EFI", 32, 6, image_size);
        } else {
            directory_entry(io_buffer + 64, "SETUP   CFG", 32, config_cluster, sizeof(SetupConfig));
        }
    }
    return write_blocks(disk, cluster_lba(volume, cluster), volume->sectors_per_cluster, io_buffer);
}

static int format_volume(BlockIO *disk, const Volume *volume, uint32_t image_clusters, int esp)
{
    zero(io_buffer, 32 * SECTOR);
    if (!write_blocks(disk, volume->start, 32, io_buffer)) return 0;
    zero(io_buffer, SECTOR);
    io_buffer[0] = 0xeb; io_buffer[1] = 0x58; io_buffer[2] = 0x90;
    copy(io_buffer + 3, "YAAIOS  ", 8);
    put16(io_buffer + 11, SECTOR);
    io_buffer[13] = volume->sectors_per_cluster;
    put16(io_buffer + 14, 32);
    io_buffer[16] = 2;
    io_buffer[21] = 0xf8;
    put16(io_buffer + 24, 63); put16(io_buffer + 26, 255);
    put32(io_buffer + 28, volume->start);
    put32(io_buffer + 32, volume->sectors);
    put32(io_buffer + 36, volume->fat_sectors);
    put32(io_buffer + 44, 2);
    put16(io_buffer + 48, 1); put16(io_buffer + 50, 6);
    io_buffer[64] = 0x80; io_buffer[66] = 0x29;
    put32(io_buffer + 67, setup_crc32(disk_guid, 16) ^ volume->start);
    copy(io_buffer + 71, esp ? "YAAI EFI   " : "YAAI DATA  ", 11);
    copy(io_buffer + 82, "FAT32   ", 8);
    put16(io_buffer + 510, 0xaa55);
    if (!write_blocks(disk, volume->start, 1, io_buffer) ||
        !write_blocks(disk, volume->start + 6, 1, io_buffer)) return 0;
    zero(io_buffer, SECTOR);
    put32(io_buffer, 0x41615252); put32(io_buffer + 484, 0x61417272);
    put32(io_buffer + 488, 0xffffffffu); put32(io_buffer + 492, 0xffffffffu);
    put32(io_buffer + 508, 0xaa550000u);
    if (!write_blocks(disk, volume->start + 1, 1, io_buffer) ||
        !write_blocks(disk, volume->start + 7, 1, io_buffer)) return 0;

    uint32_t config_cluster = 6 + image_clusters;
    for (unsigned fat_copy = 0; fat_copy < 2; ++fat_copy) {
        for (uint32_t sector = 0; sector < volume->fat_sectors;) {
            uint32_t count = volume->fat_sectors - sector;
            if (count > sizeof(io_buffer) / SECTOR) count = sizeof(io_buffer) / SECTOR;
            zero(io_buffer, count * SECTOR);
            for (uint32_t index = 0; index < count * SECTOR / 4; ++index) {
                uint32_t cluster = sector * (SECTOR / 4) + index;
                uint32_t value = 0;
                if (cluster == 0) value = 0x0ffffff8;
                else if (cluster == 1 || cluster == 2) value = 0x0fffffff;
                else if (esp && cluster >= 3 && cluster <= 5) value = 0x0fffffff;
                else if (esp && cluster >= 6 && cluster < config_cluster) {
                    value = cluster + 1 == config_cluster ? 0x0fffffff : cluster + 1;
                } else if (esp && cluster == config_cluster) value = 0x0fffffff;
                put32(io_buffer + index * 4, value);
            }
            if (!write_blocks(disk, volume->start + 32 + fat_copy * volume->fat_sectors + sector,
                              count, io_buffer)) return 0;
            sector += count;
        }
    }
    unsigned last_directory = esp ? 5 : 2;
    for (unsigned cluster = 2; cluster <= last_directory; ++cluster) {
        if (!write_directory(disk, volume, cluster, image_clusters * volume->sectors_per_cluster * SECTOR,
                             config_cluster, esp)) return 0;
    }
    return 1;
}

static void new_guid(uint8_t *guid, uint64_t *seed)
{
    for (unsigned byte = 0; byte < 16; ++byte) {
        *seed ^= *seed << 13; *seed ^= *seed >> 7; *seed ^= *seed << 17;
        guid[byte] = (uint8_t)*seed;
    }
    guid[7] = (guid[7] & 15) | 0x40;
    guid[8] = (guid[8] & 63) | 0x80;
}

static void partition(uint8_t *entry, const uint8_t *type, uint64_t first, uint64_t last,
                      const char *name, uint64_t *seed)
{
    copy(entry, type, 16);
    new_guid(entry + 16, seed);
    put64(entry + 32, first); put64(entry + 40, last);
    for (unsigned character = 0; name[character] && character < 36; ++character) {
        put16(entry + 56 + character * 2, (uint16_t)name[character]);
    }
}

static int write_gpt_header(BlockIO *disk, int backup, uint32_t entries_crc)
{
    uint64_t last = disk->media->last_block;
    zero(io_buffer, SECTOR);
    copy(io_buffer, "EFI PART", 8);
    put32(io_buffer + 8, 0x00010000); put32(io_buffer + 12, 92);
    put64(io_buffer + 24, backup ? last : 1);
    put64(io_buffer + 32, backup ? 1 : last);
    put64(io_buffer + 40, 34); put64(io_buffer + 48, last - 33);
    copy(io_buffer + 56, disk_guid, 16);
    put64(io_buffer + 72, backup ? last - 32 : 2);
    put32(io_buffer + 80, 128); put32(io_buffer + 84, 128);
    put32(io_buffer + 88, entries_crc);
    put32(io_buffer + 16, setup_crc32(io_buffer, 92));
    return write_blocks(disk, backup ? last : 1, 1, io_buffer);
}

int install_disk_write(BlockIO *disk, const uint8_t *image, uint64_t image_size,
                       SetupConfig *config)
{
    static const uint8_t esp_type[16] = {0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};
    uint64_t total = disk->media->last_block + 1;
    if (disk->media->block_size != SECTOR || !image || image_size < 512 || image_size > 32 * 1024 * 1024 ||
        total < 524288 || config->allocated_sectors > total || config->allocated_sectors < 524288 ||
        config->allocated_sectors > UINT32_MAX) return 0;
    Volume esp;
    uint32_t data_start = ESP_START + ESP_SECTORS;
    uint32_t data_sectors = (uint32_t)(config->allocated_sectors - 34 - data_start);
    if (!volume_layout(&esp, ESP_START, ESP_SECTORS) || data_sectors < 577) return 0;
    uint32_t image_clusters = (uint32_t)((image_size + esp.sectors_per_cluster * SECTOR - 1) /
                                        (esp.sectors_per_cluster * SECTOR));
    if ((uint64_t)image_clusters + 5 >= esp.clusters) return 0;
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    uint64_t seed = ((uint64_t)high << 32) | low | 1;
    new_guid(disk_guid, &seed);
    config->signature = SETUP_SIGNATURE;
    config->version = SETUP_VERSION;
    config->disk_sectors = total;
    config->esp_sectors = ESP_SECTORS;
    config->data_sectors = data_sectors;
    config->checksum = setup_crc32(config, offsetof(SetupConfig, checksum));

    // A durable marker distinguishes an interrupted install from unrelated data.
    zero(io_buffer, SECTOR);
    PendingInstall *pending = (void *)io_buffer;
    pending->signature = PENDING_SIGNATURE;
    pending->last_block = disk->media->last_block;
    pending->image_crc = setup_crc32(image, image_size);
    pending->header_crc = setup_crc32(pending, offsetof(PendingInstall, header_crc));
    if (!write_blocks(disk, 0, 1, io_buffer) || disk->flush(disk) != 0) return 0;

    if (!format_volume(disk, &esp, image_clusters, 1)) return 0;
    // The target was confirmed blank (or our interrupted install). Initialize
    // only native filesystem metadata; publish its root after zeroing banks.
    zero(io_buffer,sizeof(io_buffer));
    unsigned required=data_sectors>=FILE_V2_SECTORS?FILE_V2_SECTORS:577;
    for (unsigned offset = 0; offset < required;) {
        unsigned sectors = required - offset; if (sectors > 128) sectors = 128;
        if (!write_blocks(disk,data_start + offset,sectors,io_buffer)) return 0;
        offset += sectors;
    }
    if (disk->flush(disk)) return 0;
    documents_header(io_buffer,data_sectors,disk_guid);
    if(required==FILE_V2_SECTORS)copy(io_buffer+512,io_buffer,512);
    if (!write_blocks(disk,data_start,required==FILE_V2_SECTORS?2:1,io_buffer) || disk->flush(disk)) return 0;
    // Replace the padded directory size with the executable's exact byte length.
    if (!read_blocks(disk, cluster_lba(&esp, 4), esp.sectors_per_cluster)) return 0;
    put32(io_buffer + 64 + 28, (uint32_t)image_size);
    if (!write_blocks(disk, cluster_lba(&esp, 4), esp.sectors_per_cluster, io_buffer)) return 0;
    for (uint64_t offset = 0; offset < image_size;) {
        uint64_t bytes = image_size - offset;
        if (bytes > sizeof(io_buffer)) bytes = sizeof(io_buffer);
        uint64_t sectors = (bytes + SECTOR - 1) / SECTOR;
        zero(io_buffer, sectors * SECTOR);
        copy(io_buffer, image + offset, bytes);
        if (!write_blocks(disk, cluster_lba(&esp, 6) + offset / SECTOR, sectors, io_buffer)) return 0;
        offset += bytes;
    }
    // Verify the copied executable before publishing the partition table.
    for (uint64_t offset = 0; offset < image_size;) {
        uint64_t bytes = image_size - offset;
        if (bytes > sizeof(io_buffer)) bytes = sizeof(io_buffer);
        if (!read_blocks(disk, cluster_lba(&esp, 6) + offset / SECTOR, (bytes + SECTOR - 1) / SECTOR)) return 0;
        for (uint64_t byte = 0; byte < bytes; ++byte) {
            if (io_buffer[byte] != image[offset + byte]) return 0;
        }
        offset += bytes;
    }
    zero(io_buffer, esp.sectors_per_cluster * SECTOR);
    copy(io_buffer, config, sizeof(*config));
    if (!write_blocks(disk, cluster_lba(&esp, 6 + image_clusters), esp.sectors_per_cluster, io_buffer)) return 0;
    if (disk->flush(disk) != 0) return 0;

    zero(partition_entries, sizeof(partition_entries));
    partition(partition_entries, esp_type, ESP_START, ESP_START + ESP_SECTORS - 1, "YAAI EFI", &seed);
    partition(partition_entries + 128, documents_partition_type, data_start, (uint64_t)data_start + data_sectors - 1,
              "YAAI DATA", &seed);
    uint32_t entries_crc = setup_crc32(partition_entries, sizeof(partition_entries));
    if (!write_blocks(disk, disk->media->last_block - 32, 32, partition_entries) ||
        !write_gpt_header(disk, 1, entries_crc) || disk->flush(disk) != 0 ||
        !write_blocks(disk, 2, 32, partition_entries) || !write_gpt_header(disk, 0, entries_crc) ||
        disk->flush(disk) != 0) return 0;
    zero(io_buffer, SECTOR);
    io_buffer[446 + 4] = 0xee;
    io_buffer[447] = 0; io_buffer[448] = 2;
    io_buffer[451] = 0xff; io_buffer[452] = 0xff; io_buffer[453] = 0xff;
    put32(io_buffer + 454, 1);
    put32(io_buffer + 458, total - 1 > UINT32_MAX ? UINT32_MAX : (uint32_t)(total - 1));
    put16(io_buffer + 510, 0xaa55);
    return write_blocks(disk, 0, 1, io_buffer) && disk->flush(disk) == 0;
}
