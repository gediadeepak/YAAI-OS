#include "../platform.h"
#include "../uefi_hooks.h"
#include "../../exceptions.h"
#include "../../keyboard.h"
#include "../../mouse.h"
#include "../../ps2.h"
#include "../../audio.h"
#include "../../network.h"
#include "../../devices.h"
#include "vbox_pointer.h"

const char *platform_name(void) { return "x86-64 PC / UEFI"; }
void platform_exceptions_initialize(void) { exceptions_initialize(); }
void platform_disable_interrupts(void) { __asm__ volatile("cli" ::: "memory"); }
_Noreturn void platform_halt(void) { for (;;) __asm__ volatile("cli; hlt" ::: "memory"); }
void platform_idle(void) { __asm__ volatile("pause"); }
static void port_write(uint16_t port, uint8_t value) { __asm__ volatile("outb %0,%1" :: "a"(value), "Nd"(port)); }
static uint8_t port_read(uint16_t port) { uint8_t value; __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value; }
void platform_serial_write(const char *text)
{
    static int initialized;
    if (!initialized) {
        port_write(0x3f9,0); port_write(0x3fb,0x80); port_write(0x3f8,1);
        port_write(0x3f9,0); port_write(0x3fb,3); port_write(0x3fa,0xc7); port_write(0x3fc,0x0b);
        initialized = 1;
    }
    for (; *text; ++text) {
        unsigned timeout = 1000000;
        while (!(port_read(0x3fd) & 0x20) && --timeout) platform_idle();
        if (timeout) port_write(0x3f8,(uint8_t)*text);
    }
}
int platform_serial_console_available(void)
{
    const uint16_t scratch = 0x3ff;
    uint8_t previous = port_read(scratch);
    port_write(scratch, 0x5a);
    int first = port_read(scratch) == 0x5a;
    port_write(scratch, 0xa5);
    int second = port_read(scratch) == 0xa5;
    port_write(scratch, previous);
    return first && second;
}

char platform_serial_console_read(void)
{
    uint8_t status = port_read(0x3fd);
    if (status == 0xff || !(status & 1)) return 0;
    char key = (char)port_read(0x3f8);
    if (status & 0x1e) return 0;
    if (key == '\r') return '\n';
    if (key == 127) return '\b';
    return key;
}

void platform_wait_frame(void)
{
    uint8_t gate = port_read(0x61);
    port_write(0x61,gate & 0xfc); port_write(0x43,0xb0);
    port_write(0x42,29830 & 255); port_write(0x42,29830 >> 8); port_write(0x61,(gate & 0xfc) | 1);
    for (unsigned timeout = 0; timeout < 1000000; ++timeout) if (port_read(0x61) & 0x20) break;
}
void platform_uefi_prepare(BootServices *services, BootInfo *boot)
{
    uint64_t address = 0xffffffffULL;
    if (!services->allocate_pages(1,2,AUDIO_DMA_PAGES,&address)) boot->audio_dma = (void *)(uintptr_t)address;
}
int platform_input_initialize(unsigned width, unsigned height)
{
    int keyboard_ready = keyboard_initialize();
    mouse_reset((int)width,(int)height);
    vbox_pointer_initialize(width,height); return keyboard_ready || vbox_pointer_available();
}
int platform_key_pending(void) { return ps2_keyboard_pending(); }
unsigned platform_key_modifiers(void) { return keyboard_modifiers(); }
char platform_key_read(void) { return keyboard_read(); }
int platform_pointer_read(PlatformPointer *event)
{
    static PlatformPointer previous;
    MouseState mouse;
    PlatformPointer next = previous;
    int relative = mouse_read(&mouse);
    if (relative) next = (PlatformPointer){mouse.x,mouse.y,mouse.buttons};
    int absolute = vbox_pointer_read(&next);
    if (!relative && !absolute) return 0;
    if (next.x == previous.x && next.y == previous.y && next.buttons == previous.buttons) return 0;
    *event = previous = next; return 1;
}
int platform_pointer_available(void) { return ps2_mouse_available() || vbox_pointer_available(); }
int platform_power_transition(int restart) { return !restart && vbox_poweroff(); }
const char *platform_input_hint(void) { return "F1 Terminal | F2 System | F3 Devices | F4 Files"; }
int platform_audio_start(const BootInfo *boot) { return audio_start(boot); }
int platform_audio_available(void) { return audio_available(); }
void platform_audio_volume(unsigned percent) { audio_volume(percent); }
void platform_network_initialize(void) { network_initialize(); }
void platform_network_poll(void) { network_poll(); }
const char *platform_network_status(void) { return network_status(); }
const char *platform_network_address(void) { return network_address(); }
const char *platform_wireless_status(void) { return "Wi-Fi: use VM host networking"; }
PlatformPower platform_power(void)
{
    PowerStatus power = devices_power();
    return (PlatformPower){power.supported,power.present,power.charging,power.online,power.percent};
}
