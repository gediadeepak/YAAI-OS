#define NETWORK_TEST
#define network_initialize fixture_initialize
#define network_poll fixture_poll
#define network_status fixture_status
#define network_address fixture_address
#include "../network.c"
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

int network_test(void)
{
    uint8_t ack[256] = {0};
    ack[0] = 2; ack[1] = 1; ack[2] = 6;
    put32(ack + 4,transaction); put32(ack + 16,0x0a00020f);
    for (unsigned i = 0; i < 6; ++i) mac[i] = (uint8_t)(i + 1);
    copy(ack + 28,mac,6); put32(ack + 236,0x63825363);
    ack[240] = 53; ack[241] = 1; ack[242] = 5;
    ack[243] = 54; ack[244] = 4; put32(ack + 245,0x0a000202);
    ack[249] = 51; ack[250] = 4; put32(ack + 251,3600); ack[255] = 255;
    phase = 2; offered = 0x0a00020f; server = 0x0a000202; ip = 0;
    dhcp(ack,239); CHECK(!ip);
    ack[244] = 255; dhcp(ack,sizeof ack); CHECK(!ip); ack[244] = 4;
    ack[4] ^= 1; dhcp(ack,sizeof ack); CHECK(!ip); ack[4] ^= 1;
    ack[28] ^= 1; dhcp(ack,sizeof ack); CHECK(!ip); ack[28] ^= 1;
    dhcp(ack,255); CHECK(!ip); // Missing end option.
    put32(ack + 245,0x0a000203); dhcp(ack,sizeof ack); CHECK(!ip); put32(ack + 245,server);
    put32(ack + 251,0); dhcp(ack,sizeof ack); CHECK(!ip); put32(ack + 251,3600);
    dhcp(ack,sizeof ack); CHECK(ip == offered && phase == 3 && lease == 3600);
    CHECK(equal((const uint8_t *)address_text,(const uint8_t *)"10.0.2.15",10));
    const uint8_t sample[] = {0x45,0,0,0x14,0,0,0,0,64,1,0x66,0xe7,10,0,2,2,10,0,2,15};
    uint8_t header[20]; copy(header,sample,20); put16(header + 10,0);
    put16(header + 10,checksum(header,20,0)); CHECK(!checksum(header,20,0));
    header[19] ^= 1; CHECK(checksum(header,20,0));
    for (unsigned length = 0; length < 256; ++length) dhcp(ack,length);
    static Descriptor ring[8];
    static uint8_t buffers[8][2048];
    transmit_ring = ring; transmit_index = 0;
    for (unsigned i = 0; i < 8; ++i) transmit_buffers[i] = buffers[i];
    uint8_t arp[42] = {0};
    const uint8_t peer[] = {0x52,0x54,0,0x12,0x34,0x56};
    copy(arp + 6,peer,6); put16(arp + 12,0x806); put16(arp + 14,1);
    put16(arp + 16,0x800); arp[18] = 6; arp[19] = 4; put16(arp + 20,1);
    copy(arp + 22,peer,6); put32(arp + 28,server); put32(arp + 38,ip);
    incoming(arp,41); CHECK(!transmit_index);
    incoming(arp,42); CHECK(transmit_index == 1 && ring[0].flags == 0x8300);
    CHECK(equal(buffers[0],peer,6) && equal(buffers[0] + 22,mac,6));
    CHECK(get16(buffers[0] + 20) == 2 && get32(buffers[0] + 28) == ip && get32(buffers[0] + 38) == server);
    uint8_t ping[42] = {0};
    copy(ping + 6,peer,6); put16(ping + 12,0x800);
    ping[14] = 0x45; put16(ping + 16,28); ping[22] = 64; ping[23] = 1;
    put32(ping + 26,server); put32(ping + 30,ip);
    put16(ping + 24,checksum(ping + 14,20,0)); ping[34] = 8;
    put16(ping + 36,checksum(ping + 34,8,0));
    incoming(ping,42); CHECK(transmit_index == 2 && !buffers[1][34]);
    CHECK(!checksum(buffers[1] + 14,20,0) && !checksum(buffers[1] + 34,8,0));
    CHECK(get32(buffers[1] + 26) == ip && get32(buffers[1] + 30) == server);
    ping[41] ^= 1; incoming(ping,42); CHECK(transmit_index == 2);
    return 0;
}
