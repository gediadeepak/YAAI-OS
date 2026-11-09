#include "boot_update.h"
#include "setup.h"
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p) { return get32(p) | (uint64_t)get32(p + 4) << 32; }
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}
int boot_record_decode(const uint8_t *p, uint64_t size, BootRecord *r) {
    if (!p || size != 64 || !r)
        return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (p[i] != "YAAIBT01"[i])
            return 0;
    if (get32(p + 60) != setup_crc32(p, 60) || !get64(p + 8) || get32(p + 16) > 3 || get64(p + 24) < 512 ||
        get64(p + 24) > 32 * 1024 * 1024)
        return 0;
    for (unsigned i = 32; i < 60; ++i)
        if (p[i])
            return 0;
    *r = (BootRecord){get64(p + 8), get64(p + 24), get32(p + 16), get32(p + 20)};
    return 1;
}
void boot_record_encode(uint8_t p[64], const BootRecord *r) {
    for (unsigned i = 0; i < 64; ++i)
        p[i] = 0;
    for (unsigned i = 0; i < 8; ++i)
        p[i] = "YAAIBT01"[i];
    put64(p + 8, r->generation);
    put32(p + 16, r->slot);
    put32(p + 20, r->crc);
    put64(p + 24, r->size);
    put32(p + 60, setup_crc32(p, 60));
}
int boot_image_valid(const void *bytes, uint64_t size) {
    const uint8_t *p = bytes;
    if (!p || size < 512 || size > 32 * 1024 * 1024 || p[0] != 'M' || p[1] != 'Z')
        return 0;
    uint32_t offset = get32(p + 60);
    if (offset > size - 94)
        return 0;
    return get32(p + offset) == 0x4550 && p[offset + 4] == 0x64 && p[offset + 5] == 0x86 &&
           p[offset + 24] == 0x0b && p[offset + 25] == 2 && p[offset + 92] == 10 && p[offset + 93] == 0;
}
int boot_store_record(const BootStore *s, unsigned key, BootRecord *r) {
    uint8_t *p = 0;
    uint64_t size = 0;
    if (!s->read(s->context, key, &p, &size))
        return 0;
    int ok = boot_record_decode(p, size, r);
    s->release(s->context, p);
    return ok;
}
static unsigned image_key(unsigned slot) {
    return slot < 2 ? BOOT_SLOT0 + slot : slot == 2 ? BOOT_RECOVERY : BOOT_MAIN;
}
int boot_store_image(const BootStore *s, const BootRecord *r, uint8_t **p) {
    uint64_t size = 0;
    *p = 0;
    if (!s->read(s->context, image_key(r->slot), p, &size))
        return 0;
    if (size == r->size && boot_image_valid(*p, size) && setup_crc32(*p, size) == r->crc)
        return 1;
    s->release(s->context, *p);
    *p = 0;
    return 0;
}
static int verified_write(const BootStore *s, unsigned key, const void *p, uint64_t size) {
    if (!s->write(s->context, key, p, size))
        return 0;
    uint8_t *read = 0;
    uint64_t n = 0;
    if (!s->read(s->context, key, &read, &n))
        return 0;
    int ok = n == size && setup_crc32(read, n) == setup_crc32(p, size);
    s->release(s->context, read);
    return ok;
}
static int publish(const BootStore *s, unsigned key, const BootRecord *r) {
    uint8_t bytes[64];
    boot_record_encode(bytes, r);
    return verified_write(s, key, bytes, 64);
}
UpdateResult boot_store_update(const BootStore *s, const uint8_t *image, uint64_t size, int rollback) {
    if (!s || !s->read || !s->write || !s->release || (!rollback && !boot_image_valid(image, size)))
        return UPDATE_INVALID;
    BootRecord records[2], current = {0}, previous = {0};
    int valid[2], active = -1;
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t *p = 0;
        valid[i] = boot_store_record(s, BOOT_RECORD0 + i, &records[i]) && records[i].slot < 2 &&
                   boot_store_image(s, &records[i], &p);
        if (p)
            s->release(s->context, p);
        if (valid[i] && (active < 0 || records[i].generation > current.generation)) {
            active = (int)i;
            current = records[i];
        }
    }
    if (current.generation == UINT64_MAX)
        return UPDATE_INVALID;
    unsigned inactive = active < 0 ? 0 : 1 - current.slot;
    uint8_t *backup = 0;
    BootRecord recovery = {0}, marker = {0};
    int has_recovery = boot_store_record(s, BOOT_RECOVERY_RECORD, &recovery) && recovery.slot == 2 &&
                       boot_store_image(s, &recovery, &backup);
    if (backup) {
        s->release(s->context, backup);
        backup = 0;
    }
    if (rollback) {
        if (active < 0)
            return UPDATE_NO_PREVIOUS;
        int other = 1 - active;
        if (valid[other] && records[other].slot != current.slot)
            previous = records[other];
        else if (has_recovery) {
            previous = recovery;
            if (!boot_store_image(s, &recovery, &backup))
                return UPDATE_IO;
            int ok = verified_write(s, BOOT_SLOT0 + inactive, backup, recovery.size);
            s->release(s->context, backup);
            if (!ok)
                return UPDATE_IO;
            previous.slot = inactive;
        } else
            return UPDATE_NO_PREVIOUS;
        previous.generation = current.generation + 1;
        return publish(s, BOOT_RECORD0 + (unsigned)(1 - active), &previous) ? UPDATE_OK : UPDATE_IO;
    }
    unsigned crc = setup_crc32(image, size);
    int same = active >= 0 && current.size == size && current.crc == crc;
    int manager_ready = boot_store_record(s, BOOT_MARKER, &marker) && marker.slot == 3 &&
                        boot_store_image(s, &marker, &backup);
    if (backup) {
        s->release(s->context, backup);
        backup = 0;
    }
    if (same && manager_ready)
        return UPDATE_UNCHANGED;
    // The active slot stays untouched while the new image is written and checked.
    if (!same && !verified_write(s, BOOT_SLOT0 + inactive, image, size))
        return UPDATE_IO;
    if (!manager_ready) {
        if (!has_recovery) {
            uint64_t old_size = 0;
            if (!s->read(s->context, BOOT_MAIN, &backup, &old_size))
                return UPDATE_IO;
            if (!boot_image_valid(backup, old_size)) {
                s->release(s->context, backup);
                return UPDATE_INVALID;
            }
            recovery = (BootRecord){1, old_size, 2, setup_crc32(backup, old_size)};
            int ok = verified_write(s, BOOT_RECOVERY, backup, old_size);
            s->release(s->context, backup);
            backup = 0;
            if (!ok || !publish(s, BOOT_RECOVERY_RECORD, &recovery))
                return UPDATE_IO;
        }
        if (!verified_write(s, BOOT_MAIN, image, size))
            return UPDATE_IO;
        marker = (BootRecord){1, size, 3, crc};
        if (!publish(s, BOOT_MARKER, &marker))
            return UPDATE_IO;
    }
    if (same)
        return UPDATE_UNCHANGED;
    BootRecord next = {current.generation + 1, size, inactive, crc};
    return publish(s, BOOT_RECORD0 + (active < 0 ? 0 : (unsigned)(1 - active)), &next) ? UPDATE_OK
                                                                                       : UPDATE_IO;
}
const char *boot_update_message(UpdateResult r) {
    return r == UPDATE_OK            ? "Verified update saved."
           : r == UPDATE_UNCHANGED   ? "This version is already installed."
           : r == UPDATE_NO_PREVIOUS ? "No verified previous version is available."
           : r == UPDATE_INVALID
               ? "The boot image or update record is invalid."
               : "Update failed. Keep the installer attached for recovery; documents were not formatted.";
}
