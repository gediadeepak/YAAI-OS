#include <stddef.h>
#include "memory.h"

_Static_assert(sizeof(MemoryDescriptor) == 40, "UEFI descriptor prefix layout");
_Static_assert(offsetof(MemoryDescriptor, page_count) == 24, "UEFI descriptor page count");

static uint8_t *available, *allocated;
static uint64_t capacity, next_page;
static MemoryStats stats;
static int ready;

static int bit_set(const uint8_t *bitmap, uint64_t page)
{
    return (bitmap[page / 8] & (1u << (page % 8))) != 0;
}

int memory_initialize(const BootInfo *boot)
{
    ready = 0;
    stats.usable_pages = stats.allocated_pages = stats.free_pages = stats.retained_pages = 0;
    stats.limited_pages = 0;
    if (!boot || !boot->page_bitmaps || !boot->page_capacity ||
        boot->page_capacity > UINT64_MAX / MEMORY_PAGE_SIZE ||
        !boot->memory_map || boot->memory_descriptor_version != 1 ||
        boot->memory_descriptor_size < sizeof(MemoryDescriptor) ||
        boot->memory_descriptor_size % 8 != 0 ||
        boot->memory_map_size % boot->memory_descriptor_size != 0) {
        return 0;
    }
    capacity = boot->page_capacity;
    uint64_t bitmap_bytes = (capacity + 7) / 8;
    available = boot->page_bitmaps;
    allocated = available + bitmap_bytes;
    for (uint64_t byte = 0; byte < bitmap_bytes * 2; ++byte) {
        available[byte] = 0;
    }
    next_page = capacity;
    for (uint64_t offset = 0; offset < boot->memory_map_size; offset += boot->memory_descriptor_size) {
        const MemoryDescriptor *descriptor = (const void *)((const uint8_t *)boot->memory_map + offset);
        if (descriptor->physical_start % MEMORY_PAGE_SIZE != 0 ||
            descriptor->page_count > (UINT64_MAX - descriptor->physical_start) / MEMORY_PAGE_SIZE) {
            return 0;
        }
        // Keep all loader, boot services, runtime, and ACPI RAM reserved. This
        // also protects the current stack, page tables, image, map, and bitmaps.
        if (descriptor->type >= 1 && descriptor->type <= 10 && descriptor->type != 7) {
            if (stats.retained_pages > UINT64_MAX - descriptor->page_count) return 0;
            stats.retained_pages += descriptor->page_count;
        }
        if (descriptor->type != 7) continue;
        uint64_t start = descriptor->physical_start / MEMORY_PAGE_SIZE;
        if (start > capacity || descriptor->page_count > capacity - start) return 0;
        uint64_t end = start + descriptor->page_count;
        if (!start && end) { start = 1; ++stats.retained_pages; }
        for (uint64_t page = start; page < end; ++page) {
            // Overlapping free ranges are invalid firmware data.
            if (bit_set(available, page)) return 0;
            available[page / 8] |= (uint8_t)(1u << (page % 8));
            ++stats.usable_pages;
        }
        if (start < end && start < next_page) next_page = start;
    }
    if (boot->setup.ram_limit_mib) {
        if (boot->setup.ram_limit_mib < 128 || boot->setup.ram_limit_mib > 32768) return 0;
        uint64_t limit = boot->setup.ram_limit_mib * 256;
        if (limit <= stats.retained_pages) return 0;
        uint64_t budget = limit - stats.retained_pages;
        // Keep low pages first. Higher pages remain protected and cannot be freed.
        for (uint64_t page = capacity; stats.usable_pages > budget && page > 0;) {
            --page;
            if (bit_set(available, page)) {
                available[page / 8] &= (uint8_t)~(1u << (page % 8));
                --stats.usable_pages;
                ++stats.limited_pages;
            }
        }
    }
    stats.free_pages = stats.usable_pages;
    ready = 1;
    return 1;
}

// Single boot CPU with interrupts disabled. Add synchronization before enabling
// concurrent allocation from interrupts or additional CPUs.
uint64_t memory_allocate_page(void)
{
    if (!ready || !stats.free_pages) return 0;
    for (uint64_t page = next_page; page < capacity; ++page) {
        if (bit_set(available, page) && !bit_set(allocated, page)) {
            allocated[page / 8] |= (uint8_t)(1u << (page % 8));
            ++stats.allocated_pages;
            --stats.free_pages;
            next_page = page + 1;
            return page * MEMORY_PAGE_SIZE;
        }
    }
    return 0;
}

int memory_free_page(uint64_t address)
{
    if (!ready || address % MEMORY_PAGE_SIZE != 0) return 0;
    uint64_t page = address / MEMORY_PAGE_SIZE;
    if (page >= capacity || !bit_set(available, page) || !bit_set(allocated, page)) return 0;
    allocated[page / 8] &= (uint8_t)~(1u << (page % 8));
    --stats.allocated_pages;
    ++stats.free_pages;
    if (page < next_page) next_page = page;
    return 1;
}

MemoryStats memory_stats(void)
{
    static const MemoryStats empty;
    return ready ? stats : empty;
}
