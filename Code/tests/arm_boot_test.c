#include "../boot_info.h"
#include "../console.h"
#include "../platform/platform.h"
#include "../platform/aarch64/virtio_input.h"
int memory_test(const BootInfo *boot);
int clock_test(void);
int gui_test(void);
int input_events_test(void);
int storage_test(void);
int settings_test(void);
int settings_ui_test(void);
int display_mode_test(void);
int files_test(void);
int volumes_test(void);
int editor_test(void);
int folders_test(void);
int apps_test(void);
int browser_test(void);
int boot_update_test(void);

_Noreturn void arm_probe_main(const BootInfo *boot)
{
    platform_exceptions_initialize();
    console_initialize(boot);
    console_write("ARM64 PLATFORM TESTS\n");
    int result = memory_test(boot);
    if (!result) { console_write("MEMORY TESTS PASSED\n"); result = clock_test(); }
    if (!result) { console_write("CLOCK TESTS PASSED\n"); result = gui_test(); }
    if (!result) {
        console_write("GUI TESTS PASSED\n");
        DateTime time;
        result = !platform_clock_utc(&time) || time.year < 2020 || time.month < 1 || time.month > 12;
    }
    if (!result) {
        result = input_events_test();
        if (!result) console_write("INPUT EVENT TESTS PASSED\n");
    }
    if (!result) {
        platform_input_initialize(boot->width,boot->height);
        result = !virtio_input_keyboard_available() || !platform_pointer_available();
        if (!result) console_write("VIRTIO INPUT INITIALIZATION PASSED\n");
    }
    if (!result) { result = storage_test(); if (!result) console_write("STORAGE TESTS PASSED\n"); }
    if (!result) { result = settings_test(); if (!result) result = settings_ui_test(); if (!result) result = display_mode_test(); if (!result) console_write("DISPLAY MODE TESTS PASSED\n"); }
    if (!result) { result = files_test(); if (!result) result = volumes_test(); if (!result) result = editor_test(); if (!result) result = folders_test(); if (!result) result = apps_test(); if (!result) result = browser_test(); if (!result) result = boot_update_test(); if (!result) console_write("FILESYSTEM TESTS PASSED\n"); }
    if (!result) console_write("ARM64 TESTS PASSED\n");
    else { console_write("ARM64 TEST FAILED / LINE "); console_decimal((uint64_t)result); console_write("\n"); }
    // Test-only semihosting exit; not linked into the desktop image.
    uint64_t block[2] = {0x20026,result ? 1 : 0};
    register uint64_t operation __asm__("x0") = 0x20;
    register uint64_t *arguments __asm__("x1") = block;
    __asm__ volatile("hlt #0xf000" : "+r"(operation), "+r"(arguments) :: "memory");
    platform_halt();
}
