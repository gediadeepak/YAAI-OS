#include "vbox_pointer.h"
#include "../../devices.h"
#include "../../memory.h"

/* Wire protocol: Oracle VirtualBox include/VBox/VMMDev.h.
   This is a native YAAI driver; no Guest Additions binary is required. */
typedef struct {
    uint32_t size, version, type;
    int32_t result;
    uint32_t reserved, requestor, features;
    int32_t x, y;
} Request;
_Static_assert(sizeof(Request) == 36, "VMMDev mouse request layout");
static volatile Request *request;
static uint16_t port;
static unsigned screen_width, screen_height;
static int submit(unsigned type, unsigned size)
{
    request->size = size; request->version = 0x10001; request->type = type;
    request->result = -1; request->reserved = request->requestor = 0;
    __asm__ volatile("outl %0,%1" :: "a"((uint32_t)(uintptr_t)request), "Nd"(port) : "memory");
    return request->result >= 0;
}
void vbox_pointer_initialize(unsigned width, unsigned height)
{
    if (request || !width || !height) return;
    for (unsigned slot = 0; slot < 256; ++slot) {
        uint32_t address = 0x80000000u | slot << 8;
        if (devices_pci_read(address,0) != 0xcafe80eeu) continue;
        uint32_t bar = devices_pci_read(address,0x10);
        if (!(bar & 1) || (bar & ~3u) > 65535 || !(bar & ~3u)) return;
        uint64_t page = memory_allocate_page();
        if (!page) return;
        if (page > 0xfffff000ULL) { memory_free_page(page); return; }
        request = (volatile Request *)(uintptr_t)page;
        for (unsigned i = 0; i < sizeof(Request); ++i) ((volatile uint8_t *)request)[i] = 0;
        port = (uint16_t)(bar & ~3u); devices_pci_enable(address);
        /* Identify the protocol, with an unknown OS type. */
        request->features = 0x10004; request->x = 0;
        if (submit(50,32)) {
            request->features = 1; request->x = request->y = 0;
            if (submit(2,36)) {
                screen_width = width; screen_height = height;
                platform_serial_write("INPUT: VirtualBox absolute pointer ready\r\n");
                return;
            }
        }
        memory_free_page(page); request = 0; return;
    }
}
int vbox_pointer_available(void) { return request != 0; }
int vbox_poweroff(void)
{
    if(!request)return 0;
    request->features=2; // VMMDevPowerState_PowerOff; request body is one uint32.
    platform_serial_write("POWER: requesting VirtualBox native power-off\r\n");
    return submit(30,28);
}
int vbox_pointer_read(PlatformPointer *event)
{
    if (!request || !submit(1,36) || !(request->features & 2)) return 0;
    int x = request->x, y = request->y;
    if (x < 0 || x > 65535 || y < 0 || y > 65535) return 0;
    event->x = (int)((uint64_t)(unsigned)x * (screen_width - 1) / 65535);
    event->y = (int)((uint64_t)(unsigned)y * (screen_height - 1) / 65535);
    return 1;
}
