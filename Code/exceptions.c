#include <stdint.h>
#include "console.h"
#include "exceptions.h"

typedef struct __attribute__((packed)) {
    uint16_t offset_low, selector;
    uint8_t ist, attributes;
    uint16_t offset_middle;
    uint32_t offset_high, reserved;
} Gate;

static Gate idt[256];
extern void (*exception_entries[32])(void);
extern void unexpected_interrupt(void);

void exceptions_initialize(void)
{
    uint16_t selector;
    __asm__ volatile("mov %%cs, %0" : "=r"(selector));
    for (unsigned vector = 0; vector < 256; ++vector) {
        uintptr_t handler = (uintptr_t)(vector < 32 ? exception_entries[vector] : unexpected_interrupt);
        idt[vector].offset_low = (uint16_t)handler;
        idt[vector].selector = selector;
        idt[vector].attributes = 0x8e;
        idt[vector].offset_middle = (uint16_t)(handler >> 16);
        idt[vector].offset_high = (uint32_t)(handler >> 32);
    }
    struct __attribute__((packed)) {
        uint16_t limit;
        uintptr_t address;
    } descriptor = {sizeof(idt) - 1, (uintptr_t)idt};
    __asm__ volatile("lidt %0" : : "m"(descriptor) : "memory");
}

_Noreturn void exception_report(const uint64_t *frame)
{
    static const char *const names[32] = {
        "DIVIDE ERROR", "DEBUG", "NON MASKABLE INTERRUPT", "BREAKPOINT",
        "OVERFLOW", "BOUND RANGE", "INVALID OPCODE", "DEVICE UNAVAILABLE",
        "DOUBLE FAULT", "RESERVED", "INVALID TSS", "SEGMENT UNAVAILABLE",
        "STACK FAULT", "GENERAL PROTECTION", "PAGE FAULT", "RESERVED",
        "FLOATING POINT", "ALIGNMENT CHECK", "MACHINE CHECK", "SIMD FLOATING POINT",
        "VIRTUALIZATION", "CONTROL PROTECTION", "RESERVED", "RESERVED",
        "RESERVED", "RESERVED", "RESERVED", "RESERVED", "HYPERVISOR INJECTION",
        "VMM COMMUNICATION", "SECURITY", "RESERVED"
    };
    console_restore_fullscreen();
    console_write("YAAI OS HALTED - CPU EXCEPTION\n\n");
    console_write(frame[0] < 32 ? names[frame[0]] : "UNEXPECTED INTERRUPT");
    console_write("\nVECTOR "); console_hex(frame[0]);
    console_write("\nERROR  "); console_hex(frame[1]);
    console_write("\nRIP    "); console_hex(frame[2]);
    if (frame[0] == 14) {
        uint64_t address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(address));
        console_write("\nCR2    "); console_hex(address);
    }
    console_write("\n\nRESTART QEMU TO CONTINUE.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
