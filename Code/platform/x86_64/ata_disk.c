#include "../../block.h"
#include "../platform.h"
typedef struct {
    uint16_t base, control;
    unsigned slave;
    uint64_t sectors;
} Ata;
static Ata drives[4];
static BlockDevice devices[4];
static unsigned count;
static uint8_t in8(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static uint16_t in16(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static void out8(uint16_t port, uint8_t v) { __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(port)); }
static void out16(uint16_t port, uint16_t v) { __asm__ volatile("outw %0,%1" ::"a"(v), "Nd"(port)); }
static void settle(Ata *d) {
    for (unsigned i = 0; i < 4; ++i)
        in8(d->control);
}
static int wait(Ata *d, int data, unsigned seconds) {
    unsigned started = 0;
    int clock_ready = 0;
    for (unsigned i = 0; i < 200000000; ++i) {
        uint8_t status = in8(d->base + 7);
        if (!status || status == 255)
            return 0;
        if (!(status & 0x80)) {
            if (status & 0x21)
                return 0;
            if (!data || (status & 8))
                return 1;
        }
        // A fixed CPU-loop budget can expire before the host finishes fsync.
        // Use RTC elapsed time, with a finite fallback if the clock is absent.
        if (!(i & 4095)) {
            DateTime now;
            if (platform_clock_utc(&now)) {
                unsigned time = (unsigned)now.hour * 3600 + (unsigned)now.minute * 60 + (unsigned)now.second;
                if (!clock_ready) {
                    started = time;
                    clock_ready = 1;
                } else if ((time + 86400 - started) % 86400 >= seconds)
                    break;
            } else if (i >= 10000000)
                break;
        }
    }
    platform_serial_write("ATA: disk command timed out\r\n");
    return 0;
}
static int command(Ata *d, uint64_t sector, uint8_t operation) {
    if (sector >= d->sectors || sector >= 0x10000000ULL)
        return 0;
    out8(d->base + 6, (uint8_t)(0xe0 | d->slave << 4 | (sector >> 24)));
    settle(d);
    if (!wait(d, 0, 5))
        return 0;
    out8(d->control, 2); // Polled PIO; disable device interrupts.
    out8(d->base + 2, 1);
    out8(d->base + 3, (uint8_t)sector);
    out8(d->base + 4, (uint8_t)(sector >> 8));
    out8(d->base + 5, (uint8_t)(sector >> 16));
    out8(d->base + 7, operation);
    settle(d);
    return wait(d, 1, 5);
}
static int read_sector(void *context, uint64_t sector, void *buffer) {
    Ata *d = context;
    if (!command(d, sector, 0x20))
        return 0;
    for (unsigned i = 0; i < 256; ++i) {
        uint16_t value = in16(d->base);
        ((uint8_t *)buffer)[i * 2] = (uint8_t)value;
        ((uint8_t *)buffer)[i * 2 + 1] = (uint8_t)(value >> 8);
    }
    settle(d);
    return wait(d, 0, 5);
}
static int write_sector(void *context, uint64_t sector, const void *buffer) {
    Ata *d = context;
    if (!command(d, sector, 0x30))
        return 0;
    for (unsigned i = 0; i < 256; ++i)
        out16(d->base, (uint16_t)(((const uint8_t *)buffer)[i * 2] |
                                  (unsigned)((const uint8_t *)buffer)[i * 2 + 1] << 8));
    settle(d);
    return wait(d, 0, 5);
}
static int flush(void *context) {
    Ata *d = context;
    out8(d->base + 6, (uint8_t)(0xe0 | d->slave << 4));
    settle(d);
    if (!wait(d, 0, 5))
        return 0;
    out8(d->base + 7, 0xe7);
    settle(d);
    return wait(d, 0, 30);
}
void platform_disks_initialize(void) {
    count = 0;
    for (unsigned channel = 0; channel < 2; ++channel)
        for (unsigned slave = 0; slave < 2; ++slave) {
            Ata candidate = {(uint16_t)(channel ? 0x170 : 0x1f0), (uint16_t)(channel ? 0x376 : 0x3f6), slave,
                             0};
            Ata *d = &candidate;
            out8(d->base + 6, (uint8_t)(0xa0 | slave << 4));
            settle(d);
            if (in8(d->base + 7) == 255 || !in8(d->base + 7))
                continue;
            out8(d->base + 2, 0);
            out8(d->base + 3, 0);
            out8(d->base + 4, 0);
            out8(d->base + 5, 0);
            out8(d->base + 7, 0xec);
            settle(d);
            if (!wait(d, 1, 5) || in8(d->base + 4) || in8(d->base + 5))
                continue;
            uint16_t identify[256];
            for (unsigned i = 0; i < 256; ++i)
                identify[i] = in16(d->base);
            if (!(identify[49] & 512) || (identify[0] & 0x8000))
                continue;
            if ((identify[106] & 0xd000) == 0x5000 &&
                ((uint32_t)identify[117] | (uint32_t)identify[118] << 16) != 256)
                continue;
            d->sectors = (uint32_t)identify[60] | (uint32_t)identify[61] << 16;
            if (d->sectors > 0x10000000ULL)
                d->sectors = 0x10000000ULL;
            if (!d->sectors)
                continue;
            drives[count] = candidate;
            devices[count] = (BlockDevice){&drives[count], candidate.sectors, (identify[83] & 0x1000) != 0,
                                           read_sector,    write_sector,      flush};
            ++count;
        }
}
unsigned platform_disk_count(void) { return count; }
const BlockDevice *platform_disk(unsigned index) { return index < count ? &devices[index] : 0; }
