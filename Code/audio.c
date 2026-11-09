#include "audio.h"

extern const uint8_t startup_wav[];
extern const uint8_t startup_wav_end[];
static uint16_t active_mixer;
static unsigned desired_volume = 100;

int audio_available(void) { return active_mixer != 0; }

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void audio_volume(unsigned percent)
{
    if (percent > 100) percent = 100;
    desired_volume = percent;
    if (!active_mixer) return;
    unsigned attenuation = 31 - percent * 31 / 100;
    out16(active_mixer + 2, percent ? (uint16_t)(attenuation | (attenuation << 8)) : 0x8000);
}

static uint32_t pci_read(uint32_t device, uint8_t offset)
{
    out32(0xcf8, device | offset);
    return in32(0xcfc);
}

int audio_start(const BootInfo *boot)
{
    if (!boot->audio_dma) {
        return 0;
    }
    // Scan bus zero, where the launch script attaches the emulated AC97 card.
    uint32_t device = 0;
    for (uint32_t slot = 0; slot < 256; ++slot) {
        uint32_t address = 0x80000000u | (slot << 8);
        if (pci_read(address, 0) == 0x24158086u) {
            device = address;
            break;
        }
    }
    if (!device) {
        return 0;
    }
    uint32_t mixer_bar = pci_read(device, 0x10);
    uint32_t bus_bar = pci_read(device, 0x14);
    if (!(mixer_bar & 1) || !(bus_bar & 1) ||
        (mixer_bar & ~0xffffu) || (bus_bar & ~0xffffu)) {
        return 0;
    }
    uint16_t mixer = (uint16_t)(mixer_bar & ~3u);
    uint16_t bus = (uint16_t)(bus_bar & ~3u);
    // Enable PCI I/O and bus mastering, preserving command bits and avoiding W1C status bits.
    uint16_t command = (uint16_t)pci_read(device, 4);
    out32(0xcf8, device | 4);
    out16(0xcfc, command | 5);
    out32(bus + 0x2c, 2); // Release cold reset, stereo mode.
    unsigned timeout = 100000;
    while (!(in32(bus + 0x30) & 0x100) && --timeout) {
        __asm__ volatile("pause");
    }
    if (!timeout) {
        return 0;
    }
    out16(mixer, 0);       // Reset codec.
    out16(mixer + 2, 0);   // Unmute master volume.
    out16(mixer + 0x18, 0);// Unmute PCM output. Asset is normalized to a moderate level.
    out16(mixer + 0x2a, 0);// Fixed 48 kHz.
    out8(bus + 0x1b, 2);  // Reset PCM-out DMA engine.

    // The embedded asset is a canonical 44-byte WAV header followed by 48 kHz stereo s16 PCM.
    uint64_t asset_size = (uint64_t)(startup_wav_end - startup_wav);
    if (asset_size <= 44 || asset_size - 44 > (AUDIO_DMA_PAGES - 1) * 4096u ||
        (asset_size - 44) % 4 != 0) {
        return 0;
    }
    uint32_t byte_count = (uint32_t)(asset_size - 44);
    uint8_t *samples = (uint8_t *)boot->audio_dma + 4096;
    for (uint32_t index = 0; index < byte_count; ++index) {
        samples[index] = startup_wav[44 + index];
    }
    typedef struct {
        uint32_t address, sample_count;
    } Descriptor;
    volatile Descriptor *descriptors = boot->audio_dma;
    uint32_t sample_count = byte_count / 2;
    uint32_t offset = 0, descriptor = 0;
    while (offset < sample_count && descriptor < 32) {
        uint32_t count = sample_count - offset;
        if (count > 16384) {
            count = 16384;
        }
        descriptors[descriptor].address = (uint32_t)(uintptr_t)(samples + offset * 2);
        descriptors[descriptor].sample_count = count;
        offset += count;
        ++descriptor;
    }
    if (offset != sample_count) {
        return 0;
    }
    __asm__ volatile("" ::: "memory");
    out32(bus + 0x10, (uint32_t)(uintptr_t)descriptors);
    out8(bus + 0x15, (uint8_t)(descriptor - 1));
    out16(bus + 0x16, 0x1c);
    active_mixer = mixer;
    audio_volume(desired_volume);
    out8(bus + 0x1b, 1); // Play once, with interrupts disabled.
    return 1;
}
