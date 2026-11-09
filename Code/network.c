#include "network.h"
#include "devices.h"
#include "memory.h"
#include "screen.h"
#include "clock.h"

// Polled PCnet SWSTYLE=2 driver: separate page-backed DMA buffers, no IRQs.
typedef struct { uint32_t address; int16_t length; uint16_t flags; uint32_t count, reserved; } Descriptor;
typedef struct { uint16_t mode; uint8_t receive_length, transmit_length, mac[6]; uint16_t reserved; uint32_t filter[2], receive, transmit; } Init;
_Static_assert(sizeof(Descriptor) == 16 && sizeof(Init) == 28,"PCnet DMA layout");
static uint16_t io;
static volatile Descriptor *receive_ring, *transmit_ring;
static uint8_t *receive_buffers[8], *transmit_buffers[8], mac[6];
static unsigned receive_index, transmit_index, phase, ticks, retries, age, lease = 3600;
static uint32_t ip, offered, server, transaction = 0x59414149;
static int previous_second = -1;
static char address_text[16] = "Not configured";
static const char *status_text = "Network: AMD PCnet adapter not found";

static void out16(uint16_t port, uint16_t value) { __asm__ volatile("outw %0,%1" :: "a"(value),"Nd"(port)); }
static uint16_t in16(uint16_t port) { uint16_t value; __asm__ volatile("inw %1,%0" : "=a"(value) : "Nd"(port)); return value; }
static uint8_t in8(uint16_t port) { uint8_t value; __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port)); return value; }
static void csr(unsigned index, uint16_t value) {
#ifdef NETWORK_TEST
    (void)index; (void)value;
#else
    out16(io + 0x12,(uint16_t)index); out16(io + 0x10,value);
#endif
}
static uint16_t csr_read(unsigned index) { out16(io + 0x12,(uint16_t)index); return in16(io + 0x10); }
static void copy(uint8_t *to, const uint8_t *from, unsigned size) { for (unsigned i = 0; i < size; ++i) to[i] = from[i]; }
static int equal(const uint8_t *a, const uint8_t *b, unsigned size) { for (unsigned i = 0; i < size; ++i) if (a[i] != b[i]) return 0; return 1; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] << 8 | p[1]); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, unsigned value) { p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value; }
static void put32(uint8_t *p, uint32_t value) { p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16); p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value; }
static uint16_t checksum(const uint8_t *p, unsigned length, uint32_t sum)
{
    while (length > 1) { sum += get16(p); p += 2; length -= 2; }
    if (length) sum += (unsigned)*p << 8;
    while (sum >> 16) sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
static void address(uint32_t value)
{
    unsigned used = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        unsigned octet = (value >> shift) & 255;
        if (octet >= 100) address_text[used++] = (char)('0' + octet / 100);
        if (octet >= 10) address_text[used++] = (char)('0' + octet / 10 % 10);
        address_text[used++] = (char)('0' + octet % 10);
        if (shift) address_text[used++] = '.';
    }
    address_text[used] = 0;
}
static int transmit(const uint8_t *packet, unsigned length)
{
    volatile Descriptor *entry = &transmit_ring[transmit_index];
    if (length > 1518 || (entry->flags & 0x8000)) return 0;
    unsigned padded = length < 60 ? 60 : length;
    copy(transmit_buffers[transmit_index],packet,length);
    for (unsigned i = length; i < padded; ++i) transmit_buffers[transmit_index][i] = 0;
    entry->length = (int16_t)-(int)padded; entry->count = 0;
    __asm__ volatile("" ::: "memory");
    entry->flags = 0x8300;
    transmit_index = (transmit_index + 1) & 7;
    csr(0,8); return 1;
}
static void discover(int request)
{
    uint8_t packet[342] = {0};
    for (unsigned i = 0; i < 6; ++i) packet[i] = 255;
    copy(packet + 6,mac,6); put16(packet + 12,0x800);
    uint8_t *header = packet + 14, *udp = header + 20, *bootp = udp + 8;
    header[0] = 0x45; header[8] = 64; header[9] = 17;
    put32(header + 16,0xffffffff);
    put16(udp,68); put16(udp + 2,67);
    bootp[0] = 1; bootp[1] = 1; bootp[2] = 6;
    put32(bootp + 4,transaction); put16(bootp + 10,0x8000);
    copy(bootp + 28,mac,6); put32(bootp + 236,0x63825363);
    unsigned offset = 240;
    bootp[offset++] = 53; bootp[offset++] = 1; bootp[offset++] = request ? 3 : 1;
    bootp[offset++] = 61; bootp[offset++] = 7; bootp[offset++] = 1;
    copy(bootp + offset,mac,6); offset += 6;
    if (request) {
        bootp[offset++] = 50; bootp[offset++] = 4; put32(bootp + offset,offered); offset += 4;
        bootp[offset++] = 54; bootp[offset++] = 4; put32(bootp + offset,server); offset += 4;
    }
    bootp[offset++] = 55; bootp[offset++] = 3; bootp[offset++] = 1; bootp[offset++] = 3; bootp[offset++] = 6;
    bootp[offset++] = 255;
    unsigned payload = offset < 300 ? 300 : offset;
    put16(header + 2,payload + 28); put16(udp + 4,payload + 8);
    put16(header + 10,checksum(header,20,0));
    transmit(packet,payload + 42);
}
static void dhcp(const uint8_t *p, unsigned length)
{
    if (length < 240 || p[0] != 2 || p[1] != 1 || p[2] != 6 || get32(p + 4) != transaction ||
        !equal(p + 28,mac,6) || get32(p + 236) != 0x63825363) return;
    unsigned kind = 0, end = 0; uint32_t remote = 0, duration = 3600;
    for (unsigned offset = 240; offset < length;) {
        unsigned option = p[offset++];
        if (option == 255) { end = 1; break; }
        if (!option) continue;
        if (offset == length) return;
        unsigned size = p[offset++];
        if (size > length - offset) return;
        if (option == 53 && size == 1) kind = p[offset];
        if (option == 54 && size == 4) remote = get32(p + offset);
        if (option == 51 && size == 4) duration = get32(p + offset);
        offset += size;
    }
    uint32_t supplied = get32(p + 16);
    if (!end || !remote) return;
    if (kind == 2 && phase == 1 && supplied && (supplied >> 24) < 224) {
        offered = supplied; server = remote; phase = 2; ticks = retries = 0; discover(1);
    } else if (kind == 5 && phase == 2 && remote == server && supplied == offered && duration >= 60) {
        ip = supplied; phase = 3; age = 0; lease = duration > 86400 ? 86400 : duration;
        address(ip); status_text = "Network: Ethernet / DHCP configured";
        serial_write("NETWORK: DHCP address "); serial_write(address_text); serial_write("\r\n");
    } else if (kind == 6 && phase == 2 && remote == server) {
        phase = 1; ip = 0; ticks = retries = 0; discover(0);
    }
}
static void incoming(const uint8_t *p, unsigned length)
{
    if (length < 14) return;
    uint8_t reply[1518];
    if (get16(p + 12) == 0x806 && ip && length >= 42 && get16(p + 14) == 1 &&
        get16(p + 16) == 0x800 && p[18] == 6 && p[19] == 4 && get16(p + 20) == 1 && get32(p + 38) == ip) {
        copy(reply,p,42); copy(reply,p + 6,6); copy(reply + 6,mac,6);
        put16(reply + 20,2); copy(reply + 32,p + 22,10);
        copy(reply + 22,mac,6); put32(reply + 28,ip); transmit(reply,42); return;
    }
    if (get16(p + 12) != 0x800 || length < 34 || p[14] != 0x45) return;
    const uint8_t *header = p + 14;
    unsigned total = get16(header + 2);
    if (total < 20 || total > length - 14 || total > 1500 || (get16(header + 6) & 0x3fff) || checksum(header,20,0)) return;
    const uint8_t *payload = header + 20;
    if (header[9] == 17 && total >= 28 && get16(payload) == 67 && get16(payload + 2) == 68) {
        unsigned udp_length = get16(payload + 4);
        if (udp_length < 8 || udp_length > total - 20) return;
        uint32_t pseudo = get16(header + 12) + get16(header + 14) + get16(header + 16) + get16(header + 18) + 17 + udp_length;
        if (get16(payload + 6) && checksum(payload,udp_length,pseudo)) return;
        dhcp(payload + 8,udp_length - 8);
    } else if (ip && get32(header + 16) == ip && header[9] == 1 && total >= 28 && payload[0] == 8 && !payload[1] && !checksum(payload,total - 20,0)) {
        copy(reply,p,total + 14); copy(reply,p + 6,6); copy(reply + 6,mac,6);
        put32(reply + 26,ip); copy(reply + 30,header + 12,4);
        reply[22] = 64; put16(reply + 24,0); put16(reply + 24,checksum(reply + 14,20,0));
        reply[34] = 0; put16(reply + 36,0); put16(reply + 36,checksum(reply + 34,total - 20,0));
        transmit(reply,total + 14);
    }
}
void network_initialize(void)
{
    uint32_t device = 0;
    for (unsigned slot = 0; slot < 256; ++slot) {
        uint32_t at = 0x80000000u | slot << 8;
        if (devices_pci_read(at,0) == 0x20001022) { device = at; break; }
    }
    if (!device) return;
    uint32_t bar = devices_pci_read(device,0x10);
    if (!(bar & 1) || (bar & 0xffff0000)) return;
    io = (uint16_t)(bar & ~3u); devices_pci_enable(device);
    in16(io + 0x14); csr(0,4);
    out16(io + 0x12,20); out16(io + 0x16,2);
    for (unsigned i = 0; i < 6; ++i) mac[i] = in8(io + (uint16_t)i);
    uint64_t pages[17] = {0}; unsigned allocated = 0;
    for (; allocated < 17; ++allocated) {
        uint64_t page = memory_allocate_page();
        if (!page) break;
        if (page > 0xfffff000) { memory_free_page(page); break; }
        pages[allocated] = page;
        uint8_t *bytes = (void *)(uintptr_t)page;
        for (unsigned i = 0; i < 4096; ++i) bytes[i] = 0;
    }
    if (allocated != 17) {
        for (unsigned i = 0; i < allocated; ++i) memory_free_page(pages[i]);
        io = 0; status_text = "Network: insufficient low DMA memory"; return;
    }
    Init *init = (void *)(uintptr_t)pages[0];
    receive_ring = (void *)(uintptr_t)(pages[0] + 64); transmit_ring = (void *)(uintptr_t)(pages[0] + 256);
    init->receive_length = init->transmit_length = 0x30; copy(init->mac,mac,6);
    init->receive = (uint32_t)(uintptr_t)receive_ring; init->transmit = (uint32_t)(uintptr_t)transmit_ring;
    for (unsigned i = 0; i < 8; ++i) {
        receive_buffers[i] = (void *)(uintptr_t)pages[i + 1]; transmit_buffers[i] = (void *)(uintptr_t)pages[i + 9];
        receive_ring[i].address = (uint32_t)pages[i + 1]; receive_ring[i].length = -2048; receive_ring[i].flags = 0x8000;
        transmit_ring[i].address = (uint32_t)pages[i + 9];
    }
    uint32_t physical = (uint32_t)(uintptr_t)init;
    csr(1,(uint16_t)physical); csr(2,(uint16_t)(physical >> 16)); csr(0,1);
    unsigned timeout = 100000;
    while (!(csr_read(0) & 0x100) && --timeout) __asm__ volatile("pause");
    if (!timeout) {
        csr(0,4);
        for (unsigned i = 0; i < 17; ++i) memory_free_page(pages[i]);
        io = 0; status_text = "Network: adapter initialization failed"; return;
    }
    csr(0,0x102); phase = 1; status_text = "Network: requesting DHCP address";
    serial_write("NETWORK: PCnet ready / DHCP discover\r\n"); discover(0);
}
void network_poll(void)
{
    if (!io) return;
    for (unsigned count = 0; count < 8; ++count) {
        volatile Descriptor *entry = &receive_ring[receive_index];
        uint16_t flags = entry->flags;
        if (flags & 0x8000) break;
        __asm__ volatile("" ::: "memory");
        unsigned length = entry->count & 0xfff;
        if ((flags & 0x4300) == 0x0300 && length >= 4 && length <= 2048) incoming(receive_buffers[receive_index],length - 4);
        entry->count = 0; entry->length = -2048;
        __asm__ volatile("" ::: "memory"); entry->flags = 0x8000;
        receive_index = (receive_index + 1) & 7;
    }
    if (phase != 3 && ++ticks >= 200) {
        ticks = 0;
        if (++retries == 8) { phase = 1; retries = 0; status_text = "Network: DHCP waiting / check NAT and cable"; }
        discover(phase == 2);
    }
    if (++age % 40 == 0) {
        DateTime now;
        if (clock_local_time(0,&now)) {
            int second = now.hour * 3600 + now.minute * 60 + now.second;
            static unsigned elapsed;
            if (phase == 3 && previous_second >= 0) {
                unsigned delta = (unsigned)((second - previous_second + 86400) % 86400);
                elapsed += delta;
                if (elapsed >= lease / 2) {
                    ip = 0; phase = 1; offered = server = 0; ++transaction;
                    ticks = retries = elapsed = 0; address(0);
                    status_text = "Network: refreshing DHCP lease"; discover(0);
                }
            } else elapsed = 0;
            previous_second = second;
        }
    }
}
const char *network_status(void) { return status_text; }
const char *network_address(void) { return address_text; }
