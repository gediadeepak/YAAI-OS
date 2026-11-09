#include "power.h"
#include "files.h"
#include "uefi.h"
#include "screen.h"
#include "platform/platform.h"
#include <stddef.h>
typedef struct {
    TableHeader header;
    UnusedService before_reset[10];
    void (*reset)(uint32_t type, uint64_t status, uint64_t size, void *data);
} RuntimePower;
_Static_assert(offsetof(RuntimePower,reset) == 104,"UEFI ResetSystem ABI");
const char *power_finish(const BootInfo *boot, int restart)
{
    RuntimePower *runtime = boot ? boot->runtime_services : 0;
    if (!runtime || runtime->header.signature != 0x56524553544e5552ULL ||
        runtime->header.header_size < sizeof(*runtime) || !runtime->reset)
        return "Firmware power control is unavailable.";
    if (files_sync() != FILE_OK) return "Disk flush failed. Your session is still open.";
    serial_write(restart ? "POWER: storage flushed; restarting\r\n" : "POWER: storage flushed; shutting down\r\n");
    if(platform_power_transition(restart))platform_halt();
    runtime->reset(restart ? 0u : 2u,0,0,0);
    return "Firmware did not complete the power request.";
}
