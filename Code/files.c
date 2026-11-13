#include "files.h"
#include "screen.h"
#include "setup.h"
#include "volumes.h"
typedef struct {
    uint8_t header[512], data[FILE_BYTES];
    int valid, empty;
    uint64_t generation;
    unsigned size, parent, flags;
} Bank;
static const BlockDevice *disk;
static Bank banks[2];
static VolumeSlice slices[4];
static BlockDevice partitions[4];
enum { FILE_DRAFT = 8 };
static unsigned version, limit, max_bytes, bank_sectors;
static uint64_t base, owned_sectors;
static int upgrade_blocked;
static const char *status_text = "Documents disk unavailable";
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
static int equal(const void *a, const void *b, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (((const uint8_t *)a)[i] != ((const uint8_t *)b)[i])
            return 0;
    return 1;
}
static int normalize(const char *name, char *out) {
    if (!name)
        return 0;
    unsigned i = 0;
    for (; name[i] && i < FILE_NAME - 1; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'))
            return 0;
        out[i] = c;
    }
    out[i] = 0;
    return i && !name[i] && !(i == 1 && out[0] == '.') && !(i == 2 && out[0] == '.' && out[1] == '.');
}
static uint32_t header_crc(uint8_t *h, unsigned offset) {
    uint32_t old = get32(h + offset);
    put32(h + offset, 0);
    uint32_t crc = setup_crc32(h, 512);
    put32(h + offset, old);
    return crc;
}
static int root_valid(uint8_t *h, unsigned v, uint64_t offset, uint64_t sectors) {
    uint64_t required = v == 1 ? 577 : FILE_V2_SECTORS;
    return equal(h, v == 1 ? "YAAIFS01" : "YAAIFS02", 8) && get32(h + 8) == v && get32(h + 12) == 512 &&
           get32(h + 16) == (v == 1 ? 32 : FILE_COUNT) && get32(h + 20) == (v == 1 ? 4096 : FILE_BYTES) &&
           offset <= sectors && required <= sectors - offset && get64(h + 24) >= offset + required &&
           get64(h + 24) <= sectors && get32(h + 32) == header_crc(h, 32);
}
static void mode(unsigned v, uint64_t offset) {
    version = v;
    base = offset;
    limit = v == 1 ? 32 : FILE_COUNT;
    max_bytes = v == 1 ? 4096 : FILE_BYTES;
    bank_sectors = v == 1 ? 9 : FILE_V2_BANK;
}
FileResult files_mount(const BlockDevice *device) {
    disk = 0;
    upgrade_blocked = 0;
    status_text = "Documents disk unavailable";
    uint8_t h[512], other[512];
    if (!device || !device->read || device->sectors < 577)
        return FILE_UNAVAILABLE;
    if (!device->read(device->context, 0, h))
        return FILE_IO_ERROR;
    if (root_valid(h, 1, 0, device->sectors)) {
        mode(1, 0);
        owned_sectors = get64(h + 24);
        if (owned_sectors >= 1024 + FILE_V2_SECTORS) {
            if (!device->read(device->context, 1024, h) || !device->read(device->context, 1025, other))
                return FILE_IO_ERROR;
            if (root_valid(h, 2, 1024, owned_sectors) || root_valid(other, 2, 1024, owned_sectors))
                mode(2, 1024);
            else
                for (unsigned i = 0; i < 512; ++i)
                    if (h[i] || other[i])
                        upgrade_blocked = 1;
        }
    } else {
        if (!device->read(device->context, 1, other))
            return FILE_IO_ERROR;
        if (root_valid(h, 2, 0, device->sectors)) {
            mode(2, 0);
            owned_sectors = get64(h + 24);
        } else if (root_valid(other, 2, 0, device->sectors)) {
            mode(2, 0);
            owned_sectors = get64(other + 24);
        } else {
            status_text = "Documents volume is damaged";
            return equal(h, "YAAIFS", 6) ? FILE_CORRUPT : FILE_UNAVAILABLE;
        }
    }
    disk = device;
    status_text = upgrade_blocked ? "Damaged upgrade; original copy is read-only"
                  : version == 2  ? "Folders and 64 KiB documents ready"
                                  : "Legacy documents ready (4 KiB)";
    return FILE_OK;
}
void files_initialize(void) {
    platform_disks_initialize();
    disk = 0;
    const BlockDevice *found = 0;
    for (unsigned i = 0; i < platform_disk_count() && i < 4; ++i)
        if (documents_partition(platform_disk(i), &slices[i], &partitions[i]) == 1 &&
            files_mount(&partitions[i]) == FILE_OK) {
            if (found) {
                disk = 0;
                status_text = "Multiple system data volumes; select one disk";
                return;
            }
            found = &partitions[i];
        }
    if (found) {
        files_mount(found);
        serial_write("FILES: system data partition mounted\r\n");
        return;
    }
    for (unsigned i = 0; i < platform_disk_count(); ++i) {
        const BlockDevice *candidate = platform_disk(i);
        if (files_mount(candidate) == FILE_OK) {
            if (found) {
                disk = 0;
                status_text = "Attach only one documents volume";
                return;
            }
            found = candidate;
        }
    }
    if (found)
        files_mount(found);
    else
        status_text = "Documents disk unavailable";
    serial_write(found ? "FILES: documents mounted\r\n" : "FILES: no documents volume\r\n");
}
FileResult files_sync(void) {
    if (!disk || !disk->writable)
        return FILE_OK;
    return disk->flush && disk->flush(disk->context) ? FILE_OK : FILE_IO_ERROR;
}
static uint64_t sector(unsigned slot, unsigned bank) {
    return base + (version == 1 ? 1 : 2) + (uint64_t)(slot * 2 + bank) * bank_sectors;
}
static FileResult load(unsigned slot, unsigned index) {
    Bank *b = &banks[index];
    b->valid = b->empty = 0;
    if (!disk->read(disk->context, sector(slot, index), b->header))
        return FILE_IO_ERROR;
    unsigned nonzero = 0;
    for (unsigned i = 0; i < 512; ++i)
        nonzero |= b->header[i];
    if (!nonzero) {
        b->empty = 1;
        return FILE_NOT_FOUND;
    }
    b->size = get32(b->header + 16);
    b->generation = get64(b->header + 8);
    b->flags = version == 1 ? 0 : get32(b->header + 60);
    b->parent = version == 1 ? 0 : get32(b->header + 64);
    char name[FILE_NAME] = {0};
    if (!equal(b->header, version == 1 ? "YFILE001" : "YFILE002", 8) || !b->generation ||
        b->size > max_bytes || b->header[51] || !normalize((const char *)b->header + 20, name) ||
        !equal(name, b->header + 20, FILE_NAME) || get32(b->header + 56) != header_crc(b->header, 56) ||
        b->parent > limit || b->parent == slot + 1 || (b->flags & ~15u) || ((b->flags & FILE_DRAFT) && (b->flags != FILE_DRAFT || b->parent)) ||
        ((b->flags & (FILE_DIRECTORY | FILE_DELETED)) && b->size))
        return FILE_CORRUPT;
    unsigned sectors = version == 1 ? 8 : (b->size + 511) / 512;
    for (unsigned i = 0; i < sectors; ++i)
        if (!disk->read(disk->context, sector(slot, index) + 1 + i, b->data + i * 512))
            return FILE_IO_ERROR;
    if (get32(b->header + 52) != setup_crc32(b->data, b->size))
        return FILE_CORRUPT;
    b->valid = 1;
    return FILE_OK;
}
static FileResult select_bank(unsigned slot, int *selected) {
    FileResult a = load(slot, 0), b = load(slot, 1);
    *selected = -1;
    if (a == FILE_IO_ERROR || b == FILE_IO_ERROR)
        return FILE_IO_ERROR;
    if (banks[0].valid)
        *selected = 0;
    if (banks[1].valid && (*selected < 0 || banks[1].generation > banks[0].generation))
        *selected = 1;
    return *selected >= 0 ? FILE_OK : banks[0].empty && banks[1].empty ? FILE_NOT_FOUND : FILE_CORRUPT;
}
static void entry_from(unsigned slot, int selected, FileEntry *entry) {
    Bank *b = &banks[selected];
    for (unsigned i = 0; i < FILE_NAME; ++i)
        entry->name[i] = (char)b->header[20 + i];
    entry->id = slot + 1;
    entry->parent = b->parent;
    entry->flags = b->flags;
    entry->size = b->size;
}
FileResult files_entry(unsigned id, FileEntry *entry) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (!entry || !id || id > limit)
        return FILE_NOT_FOUND;
    int selected;
    FileResult r = select_bank(id - 1, &selected);
    if (r != FILE_OK)
        return r;
    if (banks[selected].flags & (FILE_DELETED | FILE_DRAFT))
        return FILE_NOT_FOUND;
    entry_from(id - 1, selected, entry);
    return FILE_OK;
}
static FileResult parent_valid(unsigned parent, unsigned moving, int allow_trash) {
    for (unsigned depth = 0; parent && depth < FILE_COUNT; ++depth) {
        if (parent == moving)
            return FILE_BAD_MOVE;
        FileEntry e;
        FileResult r = files_entry(parent, &e);
        if (r != FILE_OK)
            return r;
        if (!(e.flags & FILE_DIRECTORY))
            return FILE_NOT_DIRECTORY;
        if (!allow_trash && (e.flags & FILE_TRASHED))
            return FILE_BAD_MOVE;
        parent = e.parent;
    }
    return parent ? FILE_CORRUPT : FILE_OK;
}
static FileResult find(unsigned parent, const char *name, unsigned *slot, int *selected) {
    for (*slot = 0; *slot < limit; ++*slot) {
        FileResult r = select_bank(*slot, selected);
        if (r == FILE_IO_ERROR)
            return r;
        if (r == FILE_OK && !(banks[*selected].flags & (FILE_DELETED | FILE_TRASHED | FILE_DRAFT)) &&
            banks[*selected].parent == parent && equal(name, banks[*selected].header + 20, FILE_NAME))
            return FILE_OK;
    }
    return FILE_NOT_FOUND;
}
FileResult files_list_directory(unsigned parent, int trash, FileEntry *entries, unsigned capacity,
                                unsigned *count) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (!entries || !count)
        return FILE_INVALID;
    FileResult r = parent_valid(parent, 0, 1);
    if (r != FILE_OK)
        return r;
    *count = 0;
    for (unsigned slot = 0; slot < limit; ++slot) {
        int selected;
        r = select_bank(slot, &selected);
        if (r == FILE_IO_ERROR)
            return r;
        if (r != FILE_OK || banks[selected].flags & (FILE_DELETED | FILE_DRAFT))
            continue;
        Bank *b = &banks[selected];
        if (trash ? !(b->flags & FILE_TRASHED) : (b->parent != parent || (b->flags & FILE_TRASHED)))
            continue;
        if (*count >= capacity)
            return FILE_FULL;
        entry_from(slot, selected, &entries[(*count)++]);
    }
    return FILE_OK;
}
FileResult files_list(FileEntry *entries, unsigned capacity, unsigned *count) {
    return files_list_directory(0, 0, entries, capacity, count);
}
FileResult files_read_id(unsigned id, void *data, unsigned *size) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (!data || !size || !id || id > limit)
        return FILE_INVALID;
    int selected;
    FileResult r = select_bank(id - 1, &selected);
    if (r != FILE_OK)
        return r;
    Bank *b = &banks[selected];
    if (b->flags & (FILE_DELETED | FILE_DRAFT))
        return FILE_NOT_FOUND;
    if (b->flags & FILE_DIRECTORY)
        return FILE_NOT_DIRECTORY;
    unsigned capacity = *size;
    *size = b->size;
    if (capacity < b->size)
        return FILE_TOO_LARGE;
    for (unsigned i = 0; i < b->size; ++i)
        ((uint8_t *)data)[i] = b->data[i];
    return FILE_OK;
}
FileResult files_resolve(const char *path, unsigned *id) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (!path || !id)
        return FILE_INVALID;
    unsigned parent = 0, pos = path[0] == '/' ? 1 : 0;
    if (!path[pos]) {
        *id = 0;
        return FILE_OK;
    }
    for (unsigned depth = 0; depth < FILE_COUNT; ++depth) {
        char component[FILE_NAME] = {0}, name[FILE_NAME] = {0};
        unsigned n = 0;
        while (path[pos] && path[pos] != '/') {
            if (n == FILE_NAME - 1)
                return FILE_INVALID;
            component[n++] = path[pos++];
        }
        if (!normalize(component, name))
            return FILE_INVALID;
        unsigned slot;
        int selected;
        FileResult r = find(parent, name, &slot, &selected);
        if (r != FILE_OK)
            return r;
        parent = slot + 1;
        if (!path[pos]) {
            *id = parent;
            return FILE_OK;
        }
        if (!(banks[selected].flags & FILE_DIRECTORY))
            return FILE_NOT_DIRECTORY;
        if (!path[++pos])
            return FILE_INVALID;
    }
    return FILE_INVALID;
}
FileResult files_read(const char *name, void *data, unsigned *size) {
    unsigned id;
    FileResult r = files_resolve(name, &id);
    return r == FILE_OK ? files_read_id(id, data, size) : r;
}
static FileResult commit(unsigned slot, int selected, const char *name, unsigned parent, unsigned flags,
                         const void *data, unsigned size) {
    if (!disk->writable || !disk->write || !disk->flush)
        return FILE_READ_ONLY;
    uint64_t generation = selected < 0 ? 0 : banks[selected].generation;
    if (generation == UINT64_MAX)
        return FILE_FULL;
    unsigned target = selected < 0 ? 0 : (unsigned)(1 - selected);
    Bank *b = &banks[target];
    for (unsigned i = 0; i < max_bytes; ++i)
        b->data[i] = i < size ? ((const uint8_t *)data)[i] : 0;
    for (unsigned i = 0; i < 512; ++i)
        b->header[i] = 0;
    for (unsigned i = 0; i < 8; ++i)
        b->header[i] = (version == 1 ? "YFILE001" : "YFILE002")[i];
    put64(b->header + 8, generation + 1);
    put32(b->header + 16, size);
    for (unsigned i = 0; i < FILE_NAME; ++i)
        b->header[20 + i] = (uint8_t)name[i];
    put32(b->header + 52, setup_crc32(b->data, size));
    if (version == 2) {
        put32(b->header + 60, flags);
        put32(b->header + 64, parent);
    }
    put32(b->header + 56, header_crc(b->header, 56));
    uint64_t start = sector(slot, target);
    unsigned sectors = version == 1 ? 8 : (size + 511) / 512;
    for (unsigned i = 0; i < sectors; ++i)
        if (!disk->write(disk->context, start + 1 + i, b->data + i * 512)) {
            serial_write("FILES: payload write failed\r\n");
            return FILE_IO_ERROR;
        }
    if (!disk->flush(disk->context)) {
        serial_write("FILES: payload flush failed\r\n");
        return FILE_IO_ERROR;
    }
    if (!disk->write(disk->context, start, b->header)) {
        serial_write("FILES: metadata write failed\r\n");
        return FILE_IO_ERROR;
    }
    if (!disk->flush(disk->context)) {
        serial_write("FILES: metadata flush failed\r\n");
        return FILE_IO_ERROR;
    }
    FileResult checked = load(slot, target);
    if (checked != FILE_OK || banks[target].generation != generation + 1 || banks[target].size != size) {
        serial_write("FILES: committed record verification failed\r\n");
        return FILE_IO_ERROR;
    }
    for (unsigned i = 0; i < size; ++i)
        if (b->data[i] != ((const uint8_t *)data)[i]) {
            serial_write("FILES: committed payload mismatch\r\n");
            return FILE_IO_ERROR;
        }
    return FILE_OK;
}
FileResult files_upgrade(void) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (version == 2)
        return FILE_OK;
    if (upgrade_blocked)
        return FILE_CORRUPT;
    if (!disk->writable || !disk->write || !disk->flush)
        return FILE_READ_ONLY;
    if (owned_sectors < 1024 + FILE_V2_SECTORS)
        return FILE_FULL;
    const BlockDevice *device = disk;
    uint8_t zero[512] = {0}, root[512], id[16];
    if (!disk->read(disk->context, 0, root))
        return FILE_IO_ERROR;
    for (unsigned i = 0; i < 16; ++i)
        id[i] = root[40 + i];
    // Legacy sectors remain untouched until a complete extension is published.
    for (unsigned i = 0; i < FILE_COUNT * 2; ++i)
        if (!disk->write(disk->context, 1026 + (uint64_t)i * FILE_V2_BANK, zero))
            return FILE_IO_ERROR;
    if (!disk->flush(disk->context))
        return FILE_IO_ERROR;
    static uint8_t migration_data[4096];
    for (unsigned slot = 0; slot < 32; ++slot) {
        int selected;
        FileResult r = select_bank(slot, &selected);
        if (r == FILE_NOT_FOUND)
            continue;
        if (r != FILE_OK)
            return r;
        FileEntry e;
        entry_from(slot, selected, &e);
        for (unsigned i = 0; i < e.size; ++i)
            migration_data[i] = banks[selected].data[i];
        mode(2, 1024);
        r = commit(slot, -1, e.name, 0, 0, migration_data, e.size);
        mode(1, 0);
        if (r != FILE_OK)
            return r;
    }
    mode(2, 1024);
    documents_header(root, owned_sectors, id);
    int ok = disk->write(disk->context, 1025, root) && disk->flush(disk->context) &&
             disk->write(disk->context, 1024, root) && disk->flush(disk->context);
    FileResult mounted = files_mount(device);
    if (!ok || mounted != FILE_OK)
        return FILE_IO_ERROR;
    serial_write("FILES: legacy data upgraded; original copy retained\r\n");
    return FILE_OK;
}
static FileResult create_at(unsigned parent, const char *name, const void *data, unsigned size,
                            unsigned flags) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (upgrade_blocked || !disk->writable || !disk->write || !disk->flush)
        return FILE_READ_ONLY;
    char normalized[FILE_NAME] = {0};
    if ((!data && size) || !normalize(name, normalized))
        return FILE_INVALID;
    if (size > FILE_BYTES)
        return FILE_TOO_LARGE;
    if ((size > 4096 || flags || parent) && version == 1) {
        FileResult r = files_upgrade();
        if (r != FILE_OK)
            return r;
    }
    if (size > max_bytes)
        return FILE_TOO_LARGE;
    FileResult r = parent_valid(parent, 0, 0);
    if (r != FILE_OK)
        return r;
    unsigned slot;
    int selected;
    r = find(parent, normalized, &slot, &selected);
    if (r != FILE_OK && r != FILE_NOT_FOUND)
        return r;
    if (r == FILE_OK) {
        if (flags || banks[selected].flags & FILE_DIRECTORY)
            return FILE_EXISTS;
    } else {
        for (slot = 0; slot < limit; ++slot) {
            r = select_bank(slot, &selected);
            if (r == FILE_IO_ERROR)
                return r;
            if (r == FILE_NOT_FOUND) {
                selected = -1;
                break;
            }
            if (r == FILE_OK && (banks[selected].flags & FILE_DELETED))
                break;
        }
        if (slot == limit)
            return FILE_FULL;
    }
    r = commit(slot, selected, normalized, parent, flags, data, size);
    if (r == FILE_OK) {
        serial_write("FILES: saved ");
        serial_write(normalized);
        serial_write("\r\n");
    }
    return r;
}
static FileResult draft_slot(unsigned *slot, int *selected, int create) {
    if (!disk)
        return FILE_UNAVAILABLE;
    unsigned available = limit;
    int previous = -1;
    for (unsigned i = 0; i < limit; ++i) {
        int bank;
        FileResult result = select_bank(i, &bank);
        if (result == FILE_IO_ERROR)
            return result;
        if (result == FILE_OK && banks[bank].flags == FILE_DRAFT) {
            *slot = i;
            *selected = bank;
            return FILE_OK;
        }
        if (available == limit && (result == FILE_NOT_FOUND ||
            (result == FILE_OK && (banks[bank].flags & FILE_DELETED)))) {
            available = i;
            previous = result == FILE_OK ? bank : -1;
        }
    }
    if (!create)
        return FILE_NOT_FOUND;
    if (available == limit)
        return FILE_FULL;
    *slot = available;
    *selected = previous;
    return FILE_OK;
}

FileResult files_draft_read(void *data, unsigned *size) {
    unsigned slot;
    int bank;
    FileResult result = draft_slot(&slot, &bank, 0);
    if (result != FILE_OK)
        return result;
    unsigned capacity = *size;
    *size = banks[bank].size;
    if (*size > capacity)
        return FILE_TOO_LARGE;
    for (unsigned i = 0; i < *size; ++i)
        ((uint8_t *)data)[i] = banks[bank].data[i];
    return FILE_OK;
}

FileResult files_draft_write(const void *data, unsigned size) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (version == 1 || upgrade_blocked)
        return FILE_READ_ONLY;
    if (size > max_bytes)
        return FILE_TOO_LARGE;
    unsigned slot;
    int bank;
    FileResult result = draft_slot(&slot, &bank, 1);
    static const char name[FILE_NAME] = "notes-draft";
    return result == FILE_OK ? commit(slot, bank, name, 0, FILE_DRAFT, data, size) : result;
}

FileResult files_draft_clear(void) {
    unsigned slot;
    int bank;
    FileResult result = draft_slot(&slot, &bank, 0);
    static const char name[FILE_NAME] = "notes-draft";
    return result == FILE_OK ? commit(slot, bank, name, 0, FILE_DELETED, 0, 0) : result;
}

FileResult files_write_at(unsigned parent, const char *name, const void *data, unsigned size) {
    return create_at(parent, name, data, size, 0);
}
FileResult files_mkdir(unsigned parent, const char *name) {
    return create_at(parent, name, 0, 0, FILE_DIRECTORY);
}
FileResult files_write(const char *path, const void *data, unsigned size) {
    if (!path)
        return FILE_INVALID;
    unsigned end = 0, last = 0;
    for (; path[end] && end < FILE_PATH; ++end)
        if (path[end] == '/')
            last = end + 1;
    if (!end || end == FILE_PATH || !path[last])
        return FILE_INVALID;
    unsigned parent = 0;
    if (last > 1) {
        char prefix[FILE_PATH];
        for (unsigned i = 0; i < last - 1; ++i)
            prefix[i] = path[i];
        prefix[last - 1] = 0;
        FileResult r = files_resolve(prefix, &parent);
        if (r != FILE_OK)
            return r;
    }
    return files_write_at(parent, path + last, data, size);
}
static FileResult change(unsigned id, const char *new_name, unsigned parent, unsigned flags) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (upgrade_blocked)
        return FILE_READ_ONLY;
    if (version == 1 && (parent || flags)) {
        FileResult r = files_upgrade();
        if (r != FILE_OK)
            return r;
    }
    if (!id || id > limit)
        return FILE_NOT_FOUND;
    int selected;
    FileResult r = select_bank(id - 1, &selected);
    if (r != FILE_OK)
        return r;
    if (banks[selected].flags & (FILE_DELETED | FILE_DRAFT))
        return FILE_NOT_FOUND;
    char name[FILE_NAME] = {0};
    if (!normalize(new_name, name))
        return FILE_INVALID;
    return commit(id - 1, selected, name, parent, flags, banks[selected].data,
                  (flags & FILE_DELETED) ? 0 : banks[selected].size);
}
FileResult files_rename(unsigned id, const char *name) {
    FileEntry e;
    FileResult r = files_entry(id, &e);
    if (r != FILE_OK)
        return r;
    char normalized[FILE_NAME] = {0};
    if (!normalize(name, normalized))
        return FILE_INVALID;
    unsigned slot;
    int selected;
    r = find(e.parent, normalized, &slot, &selected);
    if (r == FILE_OK && slot + 1 != id)
        return FILE_EXISTS;
    if (r != FILE_OK && r != FILE_NOT_FOUND)
        return r;
    return change(id, normalized, e.parent, e.flags);
}
FileResult files_move(unsigned id, unsigned parent) {
    FileEntry e;
    FileResult r = files_entry(id, &e);
    if (r != FILE_OK)
        return r;
    if (e.flags & FILE_TRASHED)
        return FILE_BAD_MOVE;
    r = parent_valid(parent, id, 0);
    if (r != FILE_OK)
        return r;
    unsigned slot;
    int selected;
    r = find(parent, e.name, &slot, &selected);
    if (r == FILE_OK && slot + 1 != id)
        return FILE_EXISTS;
    if (r != FILE_OK && r != FILE_NOT_FOUND)
        return r;
    return change(id, e.name, parent, e.flags);
}
FileResult files_trash(unsigned id) {
    FileEntry e;
    FileResult r = files_entry(id, &e);
    return r == FILE_OK ? change(id, e.name, e.parent, e.flags | FILE_TRASHED) : r;
}
FileResult files_restore(unsigned id) {
    FileEntry e;
    FileResult r = files_entry(id, &e);
    if (r != FILE_OK)
        return r;
    if (!(e.flags & FILE_TRASHED))
        return FILE_BAD_MOVE;
    r = parent_valid(e.parent, 0, 0);
    if (r != FILE_OK)
        return r;
    unsigned slot;
    int selected;
    r = find(e.parent, e.name, &slot, &selected);
    if (r == FILE_OK)
        return FILE_EXISTS;
    if (r != FILE_NOT_FOUND)
        return r;
    return change(id, e.name, e.parent, e.flags & ~FILE_TRASHED);
}
FileResult files_remove(unsigned id) {
    FileEntry e;
    FileResult r = files_entry(id, &e);
    if (r != FILE_OK)
        return r;
    unsigned parent = id, trashed = 0;
    for (unsigned depth = 0; parent && depth < FILE_COUNT; ++depth) {
        FileEntry p;
        r = files_entry(parent, &p);
        if (r != FILE_OK)
            return r;
        trashed |= p.flags & FILE_TRASHED;
        parent = p.parent;
    }
    if (parent || !trashed)
        return FILE_BAD_MOVE;
    for (unsigned i = 1; i <= limit; ++i) {
        FileEntry child;
        r = files_entry(i, &child);
        if (r == FILE_IO_ERROR)
            return r;
        if (r == FILE_OK && child.parent == id)
            return FILE_NOT_EMPTY;
    }
    return change(id, e.name, e.parent, FILE_DELETED);
}
FileResult files_path(unsigned id, char *path, unsigned capacity) {
    if (!path || capacity < 2)
        return FILE_INVALID;
    char reversed[FILE_PATH];
    unsigned n = 0;
    for (unsigned depth = 0; id && depth < FILE_COUNT; ++depth) {
        FileEntry e;
        FileResult r = files_entry(id, &e);
        if (r != FILE_OK)
            return r;
        unsigned len = 0;
        while (e.name[len])
            ++len;
        if (n + len + 1 >= FILE_PATH)
            return FILE_INVALID;
        for (unsigned i = 0; i < len; ++i)
            reversed[n++] = e.name[len - 1 - i];
        reversed[n++] = '/';
        id = e.parent;
    }
    if (id || n + 1 > capacity)
        return FILE_INVALID;
    if (!n) {
        path[0] = '/';
        path[1] = 0;
        return FILE_OK;
    }
    for (unsigned i = 0; i < n; ++i)
        path[i] = reversed[n - 1 - i];
    path[n] = 0;
    return FILE_OK;
}
FileResult files_usage(FileUsage *usage) {
    if (!disk)
        return FILE_UNAVAILABLE;
    if (!usage)
        return FILE_INVALID;
    *usage = (FileUsage){version, 0, 0, 0, 0, max_bytes, owned_sectors};
    for (unsigned i = 1; i <= limit; ++i) {
        FileEntry e;
        int bank;
        FileResult r = select_bank(i - 1, &bank);
        if (r == FILE_OK && (banks[bank].flags & FILE_DELETED))
            continue;
        if (r == FILE_OK)
            entry_from(i - 1, bank, &e);
        if (r == FILE_IO_ERROR)
            return r;
        if (r != FILE_OK)
            continue;
        ++usage->entries;
        usage->used_bytes += e.size;
        if (e.flags & FILE_DIRECTORY)
            ++usage->folders;
        if (e.flags & FILE_TRASHED)
            ++usage->trashed;
    }
    return FILE_OK;
}
const char *files_status(void) { return status_text; }
const char *files_error(FileResult r) {
    static const char *messages[] = {"Saved",
                                     "File not found",
                                     "Documents disk unavailable",
                                     "Documents are read-only",
                                     "Use letters, numbers, dot, dash or underscore",
                                     "Document exceeds this volume's limit",
                                     "Volume full or too small to upgrade",
                                     "Disk operation failed; previous copy retained",
                                     "File or volume is damaged",
                                     "That name already exists",
                                     "Choose a folder",
                                     "Folder is not empty",
                                     "Restore parent first; folders cannot contain themselves"};
    return (unsigned)r < sizeof(messages) / sizeof(messages[0]) ? messages[r] : "File operation failed";
}
