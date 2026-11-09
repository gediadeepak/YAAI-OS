#include <stdint.h>
#include "ps2.h"

typedef struct { uint8_t bytes[256], head, tail; } Queue;
static Queue keyboard_queue, mouse_queue;
static int initialized, keyboard_ready, mouse_ready;

static uint8_t input(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void queue_byte(int auxiliary, uint8_t value)
{
    Queue *queue = auxiliary ? &mouse_queue : &keyboard_queue;
    uint8_t next = (uint8_t)(queue->head + 1);
    if (next != queue->tail) { queue->bytes[queue->head] = value; queue->head = next; }
}

static void poll(void)
{
    for (unsigned count = 0; count < 64; ++count) {
        uint8_t status = input(0x64);
        if (!(status & 1)) break;
        uint8_t value = input(0x60);
        if (!(status & 0xc0)) queue_byte((status & 0x20) != 0, value);
    }
}

static int take(Queue *queue)
{
    poll();
    if (queue->head == queue->tail) return -1;
    uint8_t value = queue->bytes[queue->tail];
    ++queue->tail;
    return value;
}

static int output(uint16_t port, uint8_t value)
{
    for (unsigned timeout = 0; timeout < 100000; ++timeout) {
        if (!(input(0x64) & 2)) {
            __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
            return 1;
        }
    }
    return 0;
}

static int response(int auxiliary)
{
    for (unsigned timeout = 0; timeout < 100000; ++timeout) {
        uint8_t status = input(0x64);
        if (!(status & 1)) continue;
        uint8_t value = input(0x60);
        if (status & 0xc0) continue;
        if (((status & 0x20) != 0) == auxiliary) return value;
        queue_byte((status & 0x20) != 0, value);
    }
    return -1;
}

static int command(int auxiliary, uint8_t value)
{
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        if (auxiliary && !output(0x64, 0xd4)) return 0;
        if (!output(0x60, value)) return 0;
        int reply = response(auxiliary);
        if (reply == 0xfa) return 1;
        if (reply != 0xfe) return 0;
    }
    return 0;
}

int ps2_initialize(void)
{
    if (initialized) return keyboard_ready;
    initialized = 1;
    if (!output(0x64, 0xad) || !output(0x64, 0xa7)) return 0;
    for (unsigned count = 0; count < 256 && (input(0x64) & 1); ++count) (void)input(0x60);
    if (!output(0x64, 0x20)) return 0;
    int configuration = response(0);
    if (configuration < 0) return 0;
    // Enable both PS/2 ports, set-1 translation, and polling instead of IRQs.
    configuration = (configuration | 0x40) & ~0x33;
    if (!output(0x64, 0x60) || !output(0x60, (uint8_t)configuration) ||
        !output(0x64, 0xae) || !output(0x64, 0xa8)) return 0;
    keyboard_ready = command(0, 0xf5) && command(0, 0xf0) && command(0, 2) && command(0, 0xf4);
    // Defaults select the standard three-byte packet format, then enable streaming.
    mouse_ready = command(1, 0xf6) && command(1, 0xf4);
    return keyboard_ready;
}

int ps2_mouse_available(void) { return mouse_ready; }
int ps2_keyboard_byte(void) { return take(&keyboard_queue); }
int ps2_mouse_byte(void) { return take(&mouse_queue); }
int ps2_keyboard_pending(void) { poll(); return keyboard_queue.head != keyboard_queue.tail; }
