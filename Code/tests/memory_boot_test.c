#include "../boot_info.h"
#include "../console.h"
#include "../exceptions.h"

int memory_test(const BootInfo *boot);
int clock_test(void);
int mouse_test(void);
int gui_test(void);
int network_test(void);
int input_events_test(void);
int storage_test(void);
int display_mode_test(void);
int files_test(void);
int volumes_test(void);
int editor_test(void);
int folders_test(void);
int apps_test(void);
int boot_update_test(void);

// Test-only entry, linked into a separate EFI image with isa-debug-exit in QEMU.
_Noreturn void memory_probe_main(const BootInfo *boot)
{
    console_initialize(boot);
    exceptions_initialize();
    int result = memory_test(boot);
    if (result) {
        console_write("MEMORY TEST FAILED AT LINE ");
        console_decimal((uint64_t)result);
        console_write("\n");
    } else {
        console_write("MEMORY TESTS PASSED\n");
        result = clock_test();
        if (result) {
            console_write("CLOCK TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n");
        } else {
            console_write("CLOCK TESTS PASSED\n");
            result = mouse_test();
            if (result) {
                console_write("MOUSE TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n");
            } else {
                console_write("MOUSE TESTS PASSED\n");
                result = gui_test();
                if (result) {
                    console_write("GUI TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n");
                } else {
                    console_write("GUI TESTS PASSED\n");
                    result = network_test();
                    if (result) {
                        console_write("NETWORK TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n");
                    } else console_write("NETWORK TESTS PASSED\n");
                }
            }
        }
    }
    if (!result) {
        result = input_events_test();
        if (!result) console_write("INPUT EVENT TESTS PASSED\n");
        else { console_write("INPUT EVENT TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n"); }
    }
    if (!result) {
        result = storage_test();
        if (!result) console_write("STORAGE TESTS PASSED\n");
        else { console_write("STORAGE TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n"); }
    }
    if (!result) {
        result = display_mode_test();
        if (!result) console_write("DISPLAY MODE TESTS PASSED\n");
        else { console_write("DISPLAY MODE TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n"); }
    }
    if (!result) {
        result = files_test(); if (!result) result = volumes_test(); if (!result) result = editor_test(); if (!result) result = folders_test(); if (!result) result = apps_test(); if (!result) result = boot_update_test();
        if (!result) console_write("FILESYSTEM TESTS PASSED\n");
        else { console_write("FILESYSTEM TEST FAILED AT LINE "); console_decimal((uint64_t)result); console_write("\n"); }
    }
    uint32_t exit_code = result ? 1 : 0;
    __asm__ volatile("outl %0, %1" : : "a"(exit_code), "Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("hlt");
}
