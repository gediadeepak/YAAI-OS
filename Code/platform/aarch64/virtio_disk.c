#include "../../block.h"
#include "../../memory.h"
#include "../platform.h"
typedef struct { uint64_t address; uint32_t length; uint16_t flags, next; } Descriptor;
typedef struct { uint32_t type, reserved; uint64_t sector; } Request;
typedef struct { volatile uint32_t *registers; uint64_t page, buffer; uint16_t consumed; int active; } VirtDisk;
static VirtDisk drives[4];
static BlockDevice devices[4];
static unsigned count;
static uint32_t reg(VirtDisk *d, unsigned offset) { return d->registers[offset / 4]; }
static void set(VirtDisk *d, unsigned offset, uint32_t value) { d->registers[offset / 4] = value; }
static void release(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void acquire(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static int reset(VirtDisk *d)
{
    set(d,0x70,0);
    for (unsigned i = 0; i < 100000; ++i) if (!reg(d,0x70)) return 1;
    return 0;
}
static int transfer(VirtDisk *d, uint32_t type, uint64_t sector, void *bytes)
{
    if (!d->active) return 0;
    uint8_t *page = (void *)(uintptr_t)d->page, *data = (void *)(uintptr_t)d->buffer;
    volatile Descriptor *descriptors = (void *)page;
    volatile uint16_t *available = (void *)(page + 1024), *used = (void *)(page + 2048);
    volatile Request *request = (void *)(page + 3072);
    volatile uint8_t *status = page + 3088;
    if (type == 1) for (unsigned i = 0; i < 512; ++i) data[i] = ((uint8_t *)bytes)[i];
    request->type = type; request->reserved = 0; request->sector = sector; *status = 255;
    descriptors[0] = (Descriptor){d->page + 3072,16,1,(uint16_t)(type == 4 ? 2 : 1)};
    descriptors[1] = (Descriptor){d->buffer,512,(uint16_t)(1 | (type == 0 ? 2 : 0)),2};
    descriptors[2] = (Descriptor){d->page + 3088,1,2,0};
    uint16_t index = available[1]; available[2 + index % 8] = 0;
    release(); available[1] = (uint16_t)(index + 1); release(); set(d,0x50,0);
    uint64_t start, now, frequency;
    __asm__ volatile("mrs %0,cntpct_el0; mrs %1,cntfrq_el0" : "=r"(start),"=r"(frequency));
    while (used[1] == d->consumed) {
        __asm__ volatile("mrs %0,cntpct_el0" : "=r"(now));
        if (now - start > frequency * 2 || (reg(d,0x70) & 64)) goto failed;
    }
    acquire();
    volatile uint32_t *entry = (void *)(page + 2052 + (d->consumed % 8) * 8);
    if ((uint16_t)(used[1] - d->consumed) != 1 || entry[0] != 0 || entry[1] != (type == 0 ? 513u : 1u)) goto failed;
    ++d->consumed;
    uint32_t interrupt = reg(d,0x60) & 3; if (interrupt) set(d,0x64,interrupt);
    if (*status) return 0;
    if (type == 0) for (unsigned i = 0; i < 512; ++i) ((uint8_t *)bytes)[i] = data[i];
    return 1;
failed:
    reset(d); d->active = 0; return 0;
}
static int read_sector(void *context, uint64_t sector, void *bytes)
{
    VirtDisk *d = context; unsigned index = (unsigned)(d - drives);
    return sector < devices[index].sectors && transfer(d,0,sector,bytes);
}
static int write_sector(void *context, uint64_t sector, const void *bytes)
{
    VirtDisk *d = context; unsigned index = (unsigned)(d - drives);
    return devices[index].writable && sector < devices[index].sectors && transfer(d,1,sector,(void *)bytes);
}
static int flush(void *context) { return transfer(context,4,0,0); }
void platform_disks_initialize(void)
{
    count = 0;
    for (unsigned slot = 0; slot < 32 && count < 4; ++slot) {
        VirtDisk d = {0}; d.registers = (void *)(uintptr_t)(0x0a000000u + slot * 512);
        if (reg(&d,0) != 0x74726976 || reg(&d,4) != 2 || reg(&d,8) != 2 || !reset(&d)) continue;
        set(&d,0x70,3); set(&d,0x14,1);
        if (!(reg(&d,0x10) & 1)) continue;
        set(&d,0x14,0); uint32_t features = reg(&d,0x10);
        set(&d,0x24,0); set(&d,0x20,features & (32 | 512)); set(&d,0x24,1); set(&d,0x20,1);
        set(&d,0x70,11); if (!(reg(&d,0x70) & 8)) continue;
        uint64_t capacity = 0;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            uint32_t generation = reg(&d,0xfc);
            capacity = reg(&d,0x100) | (uint64_t)reg(&d,0x104) << 32;
            if (generation == reg(&d,0xfc)) break;
            capacity = 0;
        }
        set(&d,0x30,0); if (!capacity || reg(&d,0x34) < 8 || reg(&d,0x44)) continue;
        d.page = memory_allocate_page(); d.buffer = memory_allocate_page();
        if (!d.page || !d.buffer) {
            if (d.page) memory_free_page(d.page);
            if (d.buffer) memory_free_page(d.buffer);
            reset(&d); continue;
        }
        uint8_t *page = (void *)(uintptr_t)d.page;
        for (unsigned i = 0; i < 4096; ++i) page[i] = 0;
        ((volatile uint16_t *)(page + 1024))[0] = 1;
        set(&d,0x38,8);
        set(&d,0x80,(uint32_t)d.page); set(&d,0x84,(uint32_t)(d.page >> 32));
        set(&d,0x90,(uint32_t)(d.page + 1024)); set(&d,0x94,(uint32_t)((d.page + 1024) >> 32));
        set(&d,0xa0,(uint32_t)(d.page + 2048)); set(&d,0xa4,(uint32_t)((d.page + 2048) >> 32));
        release(); set(&d,0x44,1); set(&d,0x70,15); d.active = 1;
        drives[count] = d;
        devices[count] = (BlockDevice){&drives[count],capacity,!(features & 32) && (features & 512),read_sector,write_sector,flush};
        ++count;
    }
}
unsigned platform_disk_count(void) { return count; }
const BlockDevice *platform_disk(unsigned index) { return index < count ? &devices[index] : 0; }
