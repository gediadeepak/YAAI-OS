#include "virtio_input.h"
#include "../../input_events.h"
#include "../../memory.h"

enum { QUEUE_SIZE = 64, MAX_DEVICES = 2 };
typedef struct { uint64_t address; uint32_t length; uint16_t flags, next; } Descriptor;
typedef struct { uint32_t id, length; } UsedEntry;
typedef struct { uint16_t type, code; int32_t value; } Event;
_Static_assert(sizeof(Descriptor) == 16 && sizeof(Event) == 8,"Virtio wire layouts");
typedef struct {
    volatile uint32_t *registers;
    uint64_t page, status_page;
    volatile uint16_t *available, *used;
    volatile UsedEntry *entries;
    volatile Event *events;
    uint16_t size, consumed;
    int keyboard, pointer, active;
} Device;
static Device devices[MAX_DEVICES];
static int initialized;
static void release(void) { __asm__ volatile("dmb oshst" ::: "memory"); }
static void acquire(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
static uint32_t reg(Device *d, unsigned offset) { return d->registers[offset / 4]; }
static void write_reg(Device *d, unsigned offset, uint32_t value) { d->registers[offset / 4] = value; }
static int reset(Device *d)
{
    write_reg(d,0x70,0);
    for (unsigned attempt = 0; attempt < 100000; ++attempt) if (!reg(d,0x70)) return 1;
    return 0;
}
static unsigned config(Device *d, unsigned select, unsigned subselect, uint8_t *data)
{
    volatile uint8_t *cfg = (volatile uint8_t *)d->registers + 0x100;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        cfg[0] = (uint8_t)select; cfg[1] = (uint8_t)subselect;
        __asm__ volatile("dmb osh" ::: "memory");
        uint32_t generation = reg(d,0xfc);
        unsigned size = cfg[2];
        if (size > 128) return 0;
        if (select == 0x12) {
            if (size != 20) return 0;
            volatile uint32_t *words = (volatile uint32_t *)(cfg + 8);
            for (unsigned i = 0; i < 5; ++i) {
                uint32_t word = words[i];
                for (unsigned byte = 0; byte < 4; ++byte) data[i * 4 + byte] = (uint8_t)(word >> (byte * 8));
            }
        } else for (unsigned i = 0; i < size; ++i) data[i] = cfg[8 + i];
        acquire();
        if (generation == reg(d,0xfc)) return size;
    }
    return 0;
}
static int capability(Device *d, unsigned type, unsigned code)
{
    uint8_t data[128]; unsigned size = config(d,0x11,type,data);
    return code / 8 < size && (data[code / 8] & (1u << (code % 8)));
}
static int32_t little32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}
static int queue(Device *d, unsigned index)
{
    write_reg(d,0x30,index);
    unsigned maximum = reg(d,0x34);
    if (reg(d,0x44) || maximum < 8) return 0;
    unsigned size = QUEUE_SIZE;
    while (size > maximum) size /= 2;
    uint64_t page = memory_allocate_page();
    if (!page) return 0;
    uint8_t *bytes = (void *)(uintptr_t)page;
    for (unsigned i = 0; i < 4096; ++i) bytes[i] = 0;
    volatile Descriptor *descriptors = (void *)bytes;
    volatile uint16_t *available = (void *)(bytes + 1024);
    available[0] = 1; // Suppress interrupts: this first driver is polled.
    if (!index) {
        for (unsigned i = 0; i < size; ++i) {
            descriptors[i].address = page + 3072 + i * sizeof(Event);
            descriptors[i].length = sizeof(Event); descriptors[i].flags = 2;
            available[2 + i] = (uint16_t)i;
        }
        available[1] = (uint16_t)size;
        d->page = page; d->available = available; d->used = (void *)(bytes + 2048);
        d->entries = (void *)(bytes + 2052); d->events = (void *)(bytes + 3072); d->size = (uint16_t)size;
    } else d->status_page = page;
    // Both eventq and the unused LED/status queue get valid ring storage.
    write_reg(d,0x38,size);
    write_reg(d,0x80,(uint32_t)page); write_reg(d,0x84,(uint32_t)(page >> 32));
    write_reg(d,0x90,(uint32_t)(page + 1024)); write_reg(d,0x94,(uint32_t)((page + 1024) >> 32));
    write_reg(d,0xa0,(uint32_t)(page + 2048)); write_reg(d,0xa4,(uint32_t)((page + 2048) >> 32));
    release(); write_reg(d,0x44,1);
    return 1;
}
void virtio_input_initialize(unsigned width, unsigned height)
{
    if (initialized) return;
    initialized = 1; input_events_initialize(width,height);
    unsigned found = 0;
    // Board-specific transport discovery. This region belongs to QEMU 'virt'.
    for (unsigned slot = 0; slot < 32 && found < MAX_DEVICES; ++slot) {
        Device candidate = {0}; candidate.registers = (void *)(uintptr_t)(0x0a000000u + slot * 0x200);
        if (reg(&candidate,0) != 0x74726976 || reg(&candidate,4) != 2 || reg(&candidate,8) != 18) continue;
        if (!reset(&candidate)) continue;
        write_reg(&candidate,0x70,3);
        write_reg(&candidate,0x14,1);
        if (!(reg(&candidate,0x10) & 1)) continue; // Require VIRTIO_F_VERSION_1.
        write_reg(&candidate,0x24,0); write_reg(&candidate,0x20,0);
        write_reg(&candidate,0x24,1); write_reg(&candidate,0x20,1);
        write_reg(&candidate,0x70,11);
        if (!(reg(&candidate,0x70) & 8)) continue;
        candidate.keyboard = capability(&candidate,1,30) && capability(&candidate,1,28);
        candidate.pointer = capability(&candidate,1,272) &&
            ((capability(&candidate,2,0) && capability(&candidate,2,1)) ||
             (capability(&candidate,3,0) && capability(&candidate,3,1)));
        if (!candidate.keyboard && !candidate.pointer) { reset(&candidate); continue; }
        if (candidate.pointer) {
            for (unsigned axis = 0; axis < 2; ++axis) {
                uint8_t data[128];
                if (config(&candidate,0x12,axis,data) >= 20)
                    input_events_absolute_range(axis,little32(data),little32(data + 4));
            }
        }
        if (!queue(&candidate,0) || !queue(&candidate,1)) {
            if (reset(&candidate)) {
                if (candidate.page) memory_free_page(candidate.page);
                if (candidate.status_page) memory_free_page(candidate.status_page);
            }
            continue;
        }
        candidate.active = 1; devices[found++] = candidate;
        release(); write_reg(&candidate,0x70,15); write_reg(&candidate,0x50,0);
        platform_serial_write(candidate.keyboard ? "INPUT: Virtio keyboard ready\r\n" : "INPUT: Virtio pointer ready\r\n");
    }
}
void virtio_input_poll(void)
{
    for (unsigned i = 0; i < MAX_DEVICES; ++i) {
        Device *d = &devices[i];
        if (!d->active) continue;
        uint16_t published = d->used[1]; acquire();
        if ((uint16_t)(published - d->consumed) > d->size || (reg(d,0x70) & 64)) goto failed;
        unsigned processed = 0;
        while (d->consumed != published && processed++ < QUEUE_SIZE) {
            unsigned at = d->consumed % d->size;
            uint32_t id = d->entries[at].id, length = d->entries[at].length;
            if (id >= d->size || length != sizeof(Event)) goto failed;
            Event event = {d->events[id].type,d->events[id].code,d->events[id].value};
            input_events_feed(event.type,event.code,event.value); ++d->consumed;
            uint16_t available = d->available[1];
            d->available[2 + available % d->size] = (uint16_t)id;
            release(); d->available[1] = (uint16_t)(available + 1);
        }
        if (processed) { release(); write_reg(d,0x50,0); }
        uint32_t interrupts = reg(d,0x60) & 3;
        if (interrupts) write_reg(d,0x64,interrupts);
        continue;
failed:
        if (reset(d)) { memory_free_page(d->page); memory_free_page(d->status_page); }
        d->active = 0;
        input_events_feed(0,3,0); input_events_feed(0,0,0);
        platform_serial_write("INPUT: invalid Virtio queue stopped\r\n");
    }
}
int virtio_input_keyboard_available(void)
{
    for (unsigned i = 0; i < MAX_DEVICES; ++i) if (devices[i].active && devices[i].keyboard) return 1;
    return 0;
}
int virtio_input_pointer_available(void)
{
    for (unsigned i = 0; i < MAX_DEVICES; ++i) if (devices[i].active && devices[i].pointer) return 1;
    return 0;
}
