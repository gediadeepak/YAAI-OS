#include "../platform.h"
#include "../uefi_hooks.h"
#include "virtio_input.h"
#include "../../input_events.h"

// QEMU 'virt' board addresses, not Samsung hardware addresses.
#define UART ((volatile uint32_t *)(uintptr_t)0x09000000)
#define RTC ((volatile uint32_t *)(uintptr_t)0x09010000)
extern const uint8_t platform_vectors[];
const char *platform_name(void) { return "ARM64 / QEMU virt / UEFI"; }
int platform_power_transition(int restart) { (void)restart;return 0; }
void platform_disable_interrupts(void) { __asm__ volatile("msr daifset, #15" ::: "memory"); }
_Noreturn void platform_halt(void) { platform_disable_interrupts(); for (;;) __asm__ volatile("wfi"); }
void platform_idle(void) { __asm__ volatile("yield"); }
void platform_exceptions_initialize(void)
{
    uintptr_t vectors = (uintptr_t)platform_vectors;
    uint64_t level;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(level));
    if (level == 8) __asm__ volatile("msr vbar_el2, %0; isb" :: "r"(vectors) : "memory");
    else if (level == 4) __asm__ volatile("msr vbar_el1, %0; isb" :: "r"(vectors) : "memory");
}
void platform_serial_write(const char *text)
{
    for (; *text; ++text) {
        unsigned timeout = 1000000;
        while ((UART[6] & 0x20) && --timeout) platform_idle();
        if (timeout) UART[0] = (uint8_t)*text;
    }
}
_Noreturn void platform_exception_report(uint64_t syndrome, uint64_t pc)
{
    platform_serial_write("ARM64 EXCEPTION: ESR=0x");
    char hex[17]; hex[16] = 0;
    for (unsigned i = 0; i < 16; ++i) hex[i] = "0123456789abcdef"[(syndrome >> ((15 - i) * 4)) & 15];
    platform_serial_write(hex); platform_serial_write(" ELR=0x");
    for (unsigned i = 0; i < 16; ++i) hex[i] = "0123456789abcdef"[(pc >> ((15 - i) * 4)) & 15];
    platform_serial_write(hex); platform_serial_write("\r\n"); platform_halt();
}
void platform_wait_frame(void)
{
    uint64_t frequency, start, now;
    __asm__ volatile("mrs %0, cntfrq_el0; mrs %1, cntpct_el0" : "=r"(frequency),"=r"(start));
    do { platform_idle(); __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now)); } while (now - start < frequency / 40);
}
void platform_uefi_prepare(BootServices *services, BootInfo *boot) { (void)services; (void)boot; }
int platform_clock_utc(DateTime *time)
{
    uint32_t seconds = RTC[0];
    if (seconds == UINT32_MAX) return 0;
    time->second = (int)(seconds % 60); time->minute = (int)(seconds / 60 % 60); time->hour = (int)(seconds / 3600 % 24);
    unsigned days = seconds / 86400, year = 1970, month = 1;
    for (;;) {
        unsigned count = 365 + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (days < count) break;
        days -= count; ++year;
    }
    static const unsigned months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    while (month < 12) {
        unsigned count = months[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        if (days < count) break;
        days -= count; ++month;
    }
    time->year = (int)year; time->month = (int)month; time->day = (int)days + 1; return 1;
}
int platform_input_initialize(unsigned width, unsigned height) { virtio_input_initialize(width,height); return 1; }
int platform_serial_console_available(void) { return 0; }
char platform_serial_console_read(void) { return 0; }
int platform_key_pending(void) { virtio_input_poll(); return input_events_key_pending() || !(UART[6] & 0x10); }
unsigned platform_key_modifiers(void) { return input_events_modifiers(); }
char platform_key_read(void)
{
    virtio_input_poll();
    if (input_events_key_pending()) return input_events_key_read();
    if (!platform_key_pending()) return 0;
    uint32_t raw = UART[0];
    if (raw & 0xf00) return 0;
    char key = (char)(raw & 255);
    // Serial-console alternatives to F1/F2/F3, until a USB input driver is added.
    if (key == 20) return 1; // Ctrl-T: Terminal
    if (key == 25) return 2; // Ctrl-Y: System
    if (key == 4) return 3;  // Ctrl-D: Devices
    if (key == 15) return 4; // Ctrl-O: Files
    if (key == '\r') return '\n';
    if (key == 127) return '\b';
    return key;
}
int platform_pointer_available(void) { return virtio_input_pointer_available(); }
const char *platform_input_hint(void) {
    return virtio_input_keyboard_available() ? "F1 Terminal | F2 System | F3 Devices | F4 Files" :
        "Serial console: Ctrl-T Terminal | Ctrl-Y System | Ctrl-D Devices | Ctrl-O Files";
}
int platform_pointer_read(PlatformPointer *event) { virtio_input_poll(); return input_events_pointer_read(event); }
int platform_audio_start(const BootInfo *boot) { (void)boot; return 0; }
int platform_audio_available(void) { return 0; }
void platform_audio_volume(unsigned percent) { (void)percent; }
void platform_network_initialize(void) {}
void platform_network_poll(void) {}
const char *platform_network_status(void) { return "Network: ARM64 driver not implemented"; }
const char *platform_network_address(void) { return "Not configured"; }
const char *platform_wireless_status(void) { return "Wi-Fi: driver not implemented"; }
PlatformPower platform_power(void) { return (PlatformPower){0,0,0,0,-1}; }
