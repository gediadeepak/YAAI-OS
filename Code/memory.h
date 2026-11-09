#ifndef MEMORY_H
#define MEMORY_H
#include "boot_info.h"

#define MEMORY_PAGE_SIZE 4096ULL

// UEFI descriptors may have extra trailing fields. Advance by the supplied stride.
typedef struct {
    uint32_t type, padding;
    uint64_t physical_start, virtual_start, page_count, attributes;
} MemoryDescriptor;

typedef struct {
    uint64_t usable_pages, allocated_pages, free_pages, retained_pages, limited_pages;
} MemoryStats;

int memory_initialize(const BootInfo *boot);
// Uninitialized physical pages. Zero means exhausted. Page zero is reserved.
uint64_t memory_allocate_page(void);
// Rejects unaligned, reserved, out-of-range, and already freed addresses.
int memory_free_page(uint64_t address);
MemoryStats memory_stats(void);
#endif
