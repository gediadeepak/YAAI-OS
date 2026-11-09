#include "devices.h"
static void write32(uint16_t port, uint32_t value) { __asm__ volatile("outl %0,%1" :: "a"(value), "Nd"(port)); }
static uint32_t read32(uint16_t port) { uint32_t value; __asm__ volatile("inl %1,%0" : "=a"(value) : "Nd"(port)); return value; }
uint32_t devices_pci_read(uint32_t address, unsigned offset)
{
    write32(0xcf8,address | (offset & 0xfc)); return read32(0xcfc);
}
void devices_pci_enable(uint32_t address)
{
    uint16_t command = (uint16_t)devices_pci_read(address,4) | 5;
    write32(0xcf8,address | 4);
    __asm__ volatile("outw %0,%1" :: "a"(command), "Nd"((uint16_t)0xcfc));
}
static uint32_t battery(unsigned index)
{
    // VirtualBox's battery interface uses four-byte indexed registers.
    write32(0x4040,index * 4); return read32(0x4044);
}
PowerStatus devices_power(void)
{
    static int platform;
    if (!platform) {
        platform = -1;
        for (unsigned slot = 0; slot < 256; ++slot)
            if (devices_pci_read(0x80000000u | slot << 8,0) == 0xcafe80eeu) platform = 1;
    }
    PowerStatus status = {0,0,0,0,-1};
    // These ports are VirtualBox-specific; never probe them on another platform.
    if (platform != 1) return status;
    uint32_t flags = battery(13);
    if (flags == UINT32_MAX || (flags & ~31u)) return status;
    status.supported = 1;
    status.present = (flags & 16) != 0;
    uint32_t source = battery(14);
    status.online = source == 1;
    if (!status.present) return status;
    status.charging = (battery(0) & 2) != 0;
    uint32_t remaining = battery(2);
    battery(4); // Refresh the capacity information before reading full capacity.
    uint32_t full = battery(6);
    if (remaining != UINT32_MAX && full && full != UINT32_MAX)
        status.percent = remaining >= full ? 100 : (int)((uint64_t)remaining * 100 / full);
    return status;
}
