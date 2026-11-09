#include "../memory.h"

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int memory_test(const BootInfo *actual_boot)
{
    typedef struct {
        MemoryDescriptor descriptor;
        uint64_t extension;
    } ExtendedDescriptor;
    static ExtendedDescriptor map[] = {
        {{7,0,8 * MEMORY_PAGE_SIZE,0,2,0},0},
        {{0,0,0,0,1,0},0},
        {{2,0,4 * MEMORY_PAGE_SIZE,0,2,0},0},
        {{7,0,MEMORY_PAGE_SIZE,0,3,0},0},
        {{11,0,10 * MEMORY_PAGE_SIZE,0,2,0},0},
        {{10,0,12 * MEMORY_PAGE_SIZE,0,1,0},0}
    };
    static uint8_t small_bitmaps[4];
    BootInfo fixture = {0};
    fixture.memory_map = map;
    fixture.memory_map_size = sizeof(map);
    fixture.memory_descriptor_size = sizeof(ExtendedDescriptor);
    fixture.memory_descriptor_version = 1;
    fixture.page_bitmaps = small_bitmaps;
    fixture.page_capacity = 16;
    CHECK(memory_initialize(&fixture));
    CHECK(memory_stats().usable_pages == 5);
    CHECK(memory_stats().retained_pages == 3);
    CHECK(!memory_free_page(0));
    CHECK(!memory_free_page(4 * MEMORY_PAGE_SIZE));
    CHECK(!memory_free_page(10 * MEMORY_PAGE_SIZE));
    CHECK(!memory_free_page(16 * MEMORY_PAGE_SIZE));
    CHECK(!memory_free_page(MEMORY_PAGE_SIZE + 1));

    uint64_t pages[5];
    for (unsigned index = 0; index < 5; ++index) {
        pages[index] = memory_allocate_page();
        CHECK(pages[index] != 0 && pages[index] % MEMORY_PAGE_SIZE == 0);
        CHECK(pages[index] < 4 * MEMORY_PAGE_SIZE ||
              (pages[index] >= 8 * MEMORY_PAGE_SIZE && pages[index] < 10 * MEMORY_PAGE_SIZE));
        for (unsigned previous = 0; previous < index; ++previous) {
            CHECK(pages[index] != pages[previous]);
        }
    }
    CHECK(!memory_allocate_page());
    CHECK(memory_stats().allocated_pages == 5 && memory_stats().free_pages == 0);
    CHECK(memory_free_page(pages[1]));
    CHECK(memory_allocate_page() == pages[1]);
    CHECK(memory_free_page(pages[1]));
    CHECK(!memory_free_page(pages[1]));
    for (unsigned index = 0; index < 5; ++index) {
        if (index != 1) CHECK(memory_free_page(pages[index]));
    }
    CHECK(memory_stats().allocated_pages == 0 && memory_stats().free_pages == 5);

    // Reject malformed maps and keep failed initialization unusable.
    fixture.memory_descriptor_size = 32;
    CHECK(!memory_initialize(&fixture) && !memory_allocate_page());
    fixture.memory_descriptor_size = sizeof(ExtendedDescriptor);
    fixture.memory_map_size = sizeof(map) - 1;
    CHECK(!memory_initialize(&fixture));
    fixture.memory_map_size = sizeof(map);
    fixture.page_capacity = 9;
    CHECK(!memory_initialize(&fixture));
    CHECK(memory_stats().usable_pages == 0);
    fixture.page_capacity = 16;
    uint64_t saved_address = map[0].descriptor.physical_start;
    map[0].descriptor.physical_start = MEMORY_PAGE_SIZE;
    CHECK(!memory_initialize(&fixture));
    map[0].descriptor.physical_start = saved_address;

    // Physical addresses above 4 GiB must retain their high bits.
    static MemoryDescriptor high_map = {7,0,0x100000000ULL,0,3,0};
    static uint8_t high_bitmaps[262146];
    fixture.memory_map = &high_map;
    fixture.memory_map_size = fixture.memory_descriptor_size = sizeof(high_map);
    fixture.page_capacity = 0x100000 + 4;
    fixture.page_bitmaps = high_bitmaps;
    CHECK(memory_initialize(&fixture));
    uint64_t high_page = memory_allocate_page();
    CHECK(high_page == 0x100000000ULL && memory_free_page(high_page));

    // Page zero stays reserved even when firmware describes it as conventional.
    high_map.physical_start = 0;
    fixture.page_capacity = 3;
    fixture.page_bitmaps = small_bitmaps;
    CHECK(memory_initialize(&fixture));
    CHECK(memory_stats().usable_pages == 2 && !memory_free_page(0));
    CHECK(memory_allocate_page() == MEMORY_PAGE_SIZE);
    high_map.physical_start = 0x100000000ULL;

    static MemoryDescriptor limited_map = {7,0,0,0,131072,0};
    static uint8_t limited_bitmaps[32768];
    fixture.memory_map = &limited_map;
    fixture.memory_map_size = fixture.memory_descriptor_size = sizeof(limited_map);
    fixture.page_capacity = 131072;
    fixture.page_bitmaps = limited_bitmaps;
    fixture.setup.ram_limit_mib = 128;
    CHECK(memory_initialize(&fixture));
    CHECK(memory_stats().usable_pages == 32767 && memory_stats().limited_pages == 98304);
    for (uint64_t page = 1; page < 32768; ++page) CHECK(memory_allocate_page() == page * MEMORY_PAGE_SIZE);
    CHECK(!memory_allocate_page() && !memory_free_page(128 * 1024 * 1024));
    for (uint64_t page = 1; page < 32768; ++page) CHECK(memory_free_page(page * MEMORY_PAGE_SIZE));
    fixture.setup.ram_limit_mib = 127;
    CHECK(!memory_initialize(&fixture));
    fixture.setup.ram_limit_mib = 1024;
    CHECK(memory_initialize(&fixture) && memory_stats().limited_pages == 0);

    // Exercise actual RAM after ExitBootServices, including writes and reuse.
    CHECK(memory_initialize(actual_boot));
    MemoryStats before = memory_stats();
    CHECK(before.free_pages >= 3);
    for (unsigned index = 0; index < 3; ++index) {
        pages[index] = memory_allocate_page();
        CHECK(pages[index] != 0 && pages[index] % MEMORY_PAGE_SIZE == 0);
        unsigned matches = 0;
        for (uint64_t offset = 0; offset < actual_boot->memory_map_size;
             offset += actual_boot->memory_descriptor_size) {
            const MemoryDescriptor *entry = (const void *)((const uint8_t *)actual_boot->memory_map + offset);
            if (pages[index] >= entry->physical_start &&
                pages[index] - entry->physical_start < entry->page_count * MEMORY_PAGE_SIZE) {
                CHECK(entry->type == 7);
                ++matches;
            }
        }
        CHECK(matches == 1);
        volatile uint64_t *words = (void *)(uintptr_t)pages[index];
        for (unsigned word = 0; word < MEMORY_PAGE_SIZE / sizeof(uint64_t); ++word) {
            words[word] = 0xaabbccdd00000000ULL | index;
        }
    }
    for (unsigned index = 0; index < 3; ++index) {
        volatile uint64_t *words = (void *)(uintptr_t)pages[index];
        for (unsigned word = 0; word < MEMORY_PAGE_SIZE / sizeof(uint64_t); ++word) {
            CHECK(words[word] == (0xaabbccdd00000000ULL | index));
        }
    }
    CHECK(memory_stats().allocated_pages == 3 && memory_stats().free_pages == before.free_pages - 3);
    CHECK(!memory_free_page((uint64_t)(uintptr_t)actual_boot->memory_map & ~(MEMORY_PAGE_SIZE - 1)));
    CHECK(!memory_free_page((uint64_t)(uintptr_t)actual_boot->page_bitmaps & ~(MEMORY_PAGE_SIZE - 1)));
    CHECK(!memory_free_page((uint64_t)(uintptr_t)actual_boot->audio_dma));
    CHECK(!memory_free_page((uint64_t)(uintptr_t)actual_boot->framebuffer & ~(MEMORY_PAGE_SIZE - 1)));
    CHECK(!memory_free_page((uint64_t)(uintptr_t)kernel_main & ~(MEMORY_PAGE_SIZE - 1)));
    CHECK(!memory_free_page((uint64_t)(uintptr_t)&before & ~(MEMORY_PAGE_SIZE - 1)));
    for (unsigned index = 0; index < 3; ++index) CHECK(memory_free_page(pages[index]));
    CHECK(memory_allocate_page() == pages[0]);
    CHECK(memory_free_page(pages[0]) && !memory_free_page(pages[0]));
    CHECK(memory_stats().free_pages == before.free_pages && memory_stats().allocated_pages == 0);
    return 0;
}
