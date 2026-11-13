#ifndef YAAI_PLATFORM_H
#define YAAI_PLATFORM_H
#include "../boot_info.h"
#include "../clock.h"

// The shared kernel calls this contract; exactly one backend is linked per build.
typedef struct { int x, y; uint8_t buttons; } PlatformPointer;
typedef struct { int supported, present, charging, online, percent; } PlatformPower;
const char *platform_name(void);
void platform_storage_initialize(const BootInfo *boot);
void platform_exceptions_initialize(void);
void platform_disable_interrupts(void);
_Noreturn void platform_halt(void);
void platform_idle(void);
void platform_serial_write(const char *text);
int platform_serial_console_available(void);
char platform_serial_console_read(void);
int platform_remote_serial_initialize(void);
int platform_remote_serial_read(void);
unsigned platform_remote_serial_write(const uint8_t *bytes, unsigned size);
void platform_wait_frame(void);
int platform_clock_utc(DateTime *time);
int platform_input_initialize(unsigned width, unsigned height);
int platform_key_pending(void);
char platform_key_read(void);
enum { INPUT_CTRL = 1, INPUT_SHIFT = 2 };
unsigned platform_key_modifiers(void);
int platform_pointer_read(PlatformPointer *event);
int platform_pointer_available(void);
const char *platform_input_hint(void);
int platform_audio_start(const BootInfo *boot);
int platform_audio_available(void);
void platform_audio_volume(unsigned percent);
void platform_network_initialize(void);
void platform_network_poll(void);
const char *platform_network_status(void);
const char *platform_network_address(void);
const char *platform_wireless_status(void);
PlatformPower platform_power(void);
// Return nonzero only when a native platform transition was accepted.
int platform_power_transition(int restart);
#endif
