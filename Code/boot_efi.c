#include "boot_efi.h"
#include <stddef.h>
_Static_assert(offsetof(BootServices, load_image) == 200, "UEFI LoadImage offset");
_Static_assert(offsetof(BootServices, start_image) == 208, "UEFI StartImage offset");
_Static_assert(offsetof(File, get_info) == 64, "UEFI GetInfo offset");
_Static_assert(offsetof(File, set_info) == 72, "UEFI SetInfo offset");
static const Guid loaded_guid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const Guid filesystem_guid = {
    0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const Guid path_guid = {0x09576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const Guid info_guid = {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const uint64_t child_marker = UINT64_C(0x594141494348494c);
static const uint16_t *paths[BOOT_FILE_COUNT] = {u"\\EFI\\BOOT\\BOOTX64.EFI", u"\\EFI\\YAAI\\RECOVER.EFI",
                                                 u"\\EFI\\YAAI\\RECOVER.CFG", u"\\EFI\\YAAI\\SLOT0.EFI",
                                                 u"\\EFI\\YAAI\\SLOT1.EFI",   u"\\EFI\\YAAI\\BOOT0.CFG",
                                                 u"\\EFI\\YAAI\\BOOT1.CFG",   u"\\EFI\\YAAI\\MANAGER.CFG"};
static int read_file(void *context, unsigned key, uint8_t **bytes, uint64_t *size) {
    EfiBootStore *c = context;
    File *file = 0;
    *bytes = 0;
    *size = 0;
    if (key >= BOOT_FILE_COUNT || c->root->open(c->root, &file, paths[key], 1, 0))
        return 0;
    int ok = 0;
    if (!file->set_position(file, UINT64_MAX) && !file->get_position(file, size) && *size &&
        *size <= 32 * 1024 * 1024 && !file->set_position(file, 0) &&
        !c->services->allocate_pool(2, *size, (void **)bytes)) {
        uint64_t n = *size;
        ok = !file->read(file, &n, *bytes) && n == *size;
        if (!ok) {
            c->services->free_pool(*bytes);
            *bytes = 0;
        }
    }
    file->close(file);
    return ok;
}
static void release(void *context, uint8_t *bytes) {
    EfiBootStore *c = context;
    if (bytes)
        c->services->free_pool(bytes);
}
static int write_file(void *context, unsigned key, const void *bytes, uint64_t size) {
    EfiBootStore *c = context;
    File *directory = 0, *file = 0;
    if (key >= BOOT_FILE_COUNT || !size || size > 32 * 1024 * 1024)
        return 0;
    if (c->root->open(c->root, &directory, u"\\EFI\\YAAI", UINT64_C(0x8000000000000003), 16))
        return 0;
    directory->close(directory);
    if (c->root->open(c->root, &file, paths[key], UINT64_C(0x8000000000000003), 0))
        return 0;
    uint64_t info_size = 0;
    void *info = 0;
    int ok = 0;
    if (file->get_info && file->set_info &&
        file->get_info(file, &info_guid, &info_size, 0) == UINT64_C(0x8000000000000005) && info_size >= 82 &&
        info_size <= 4096 && !c->services->allocate_pool(2, info_size, &info)) {
        if (!file->get_info(file, &info_guid, &info_size, info)) {
            ((uint64_t *)info)[1] = size;
            if (!file->set_info(file, &info_guid, info_size, info) && !file->set_position(file, 0)) {
                uint64_t written = size;
                ok = !file->write(file, &written, bytes) && written == size && !file->flush(file);
            }
        }
        c->services->free_pool(info);
    }
    if (file->close(file))
        ok = 0;
    return ok;
}
BootStore boot_efi_store(EfiBootStore *c, File *root, BootServices *services) {
    *c = (EfiBootStore){root, services};
    return (BootStore){c, read_file, release, write_file};
}
static unsigned path_size(const uint8_t *path) {
    for (unsigned offset = 0; offset < 4096;) {
        unsigned n = path[offset + 2] | (unsigned)path[offset + 3] << 8;
        if (n < 4 || n > 4096 - offset)
            return 0;
        if (path[offset] == 0x7f)
            return offset;
        offset += n;
    }
    return 0;
}
uint64_t boot_efi_start(SystemTable *system, void *parent, void *device, File *root, unsigned key,
                        int skip_dispatch) {
    BootServices *s = system->boot_services;
    EfiBootStore context;
    BootStore store = boot_efi_store(&context, root, s);
    uint8_t *image = 0, *prefix = 0, *path = 0;
    uint64_t size = 0;
    void *child = 0;
    uint64_t status = UINT64_C(0x8000000000000001);
    if (!store.read(store.context, key, &image, &size) || !boot_image_valid(image, size))
        goto done;
    if (s->handle_protocol(device, &path_guid, (void **)&prefix))
        goto done;
    unsigned before = path_size(prefix), chars = 0;
    if (!before)
        goto done;
    while (paths[key][chars])
        ++chars;
    unsigned node = 4 + (chars + 1) * 2, total = before + node + 4;
    if (s->allocate_pool(2, total, (void **)&path))
        goto done;
    for (unsigned i = 0; i < before; ++i)
        path[i] = prefix[i];
    path[before] = 4;
    path[before + 1] = 4;
    path[before + 2] = (uint8_t)node;
    path[before + 3] = (uint8_t)(node >> 8);
    for (unsigned i = 0; i <= chars; ++i) {
        path[before + 4 + i * 2] = (uint8_t)paths[key][i];
        path[before + 5 + i * 2] = (uint8_t)(paths[key][i] >> 8);
    }
    path[before + node] = 0x7f;
    path[before + node + 1] = 0xff;
    path[before + node + 2] = 4;
    path[before + node + 3] = 0;
    status = s->load_image(0, parent, path, image, size, &child);
    if (!status) {
        LoadedImage *loaded = 0;
        if (skip_dispatch) {
            status = s->handle_protocol(child, &loaded_guid, (void **)&loaded);
            if (status)
                goto done;
            loaded->load_options = (void *)&child_marker;
            loaded->load_options_size = sizeof(child_marker);
        }
        uint64_t exit_size = 0;
        uint16_t *exit_data = 0;
        status = s->start_image(child, &exit_size, &exit_data);
        if (exit_data)
            s->free_pool(exit_data);
    }
done:
    if (image)
        store.release(store.context, image);
    if (path)
        s->free_pool(path);
    return status;
}
void boot_slots_start(void *handle, SystemTable *system) {
    BootServices *s = system->boot_services;
    LoadedImage *loaded = 0;
    FileSystem *fs = 0;
    File *root = 0;
    if (s->handle_protocol(handle, &loaded_guid, (void **)&loaded))
        return;
    if (loaded->load_options_size == sizeof(child_marker) && loaded->load_options &&
        *(const uint64_t *)loaded->load_options == child_marker)
        return;
    if (s->handle_protocol(loaded->device, &filesystem_guid, (void **)&fs) || fs->open_volume(fs, &root))
        return;
    EfiBootStore context;
    BootStore store = boot_efi_store(&context, root, s);
    BootRecord records[2];
    int valid[2], first = -1;
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t *image = 0;
        valid[i] = boot_store_record(&store, BOOT_RECORD0 + i, &records[i]) && records[i].slot < 2 &&
                   boot_store_image(&store, &records[i], &image);
        if (image)
            store.release(store.context, image);
        if (valid[i] && (first < 0 || records[i].generation > records[first].generation))
            first = (int)i;
    }
    if (first < 0) {
        root->close(root);
        return;
    }
    system->console_out->output_string(system->console_out,
                                       u"YAAI boot: press R for the previous version (2 seconds).\r\n");
    for (unsigned wait = 0; wait < 2000; ++wait) {
        InputKey key;
        if (!system->console_in->read_key(system->console_in, &key)) {
            if (key.unicode == 'r' || key.unicode == 'R') {
                UpdateResult result = boot_store_update(&store, 0, 0, 1);
                if (result == UPDATE_OK) {
                    for (unsigned i = 0; i < 2; ++i)
                        valid[i] =
                            boot_store_record(&store, BOOT_RECORD0 + i, &records[i]) && records[i].slot < 2;
                    first = valid[1] && (!valid[0] || records[1].generation > records[0].generation) ? 1 : 0;
                } else
                    system->console_out->output_string(
                        system->console_out,
                        u"No previous version available. Starting the current system.\r\n");
            }
            break;
        }
        s->stall(1000);
    }
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        unsigned index = attempt ? (unsigned)(1 - first) : (unsigned)first;
        if (!valid[index])
            continue;
        system->console_out->output_string(system->console_out,
                                           u"BOOT: starting verified installed slot\r\n");
        if (!boot_efi_start(system, handle, loaded->device, root, BOOT_SLOT0 + records[index].slot, 1)) {
            root->close(root);
            return;
        }
    }
    BootRecord recovery;
    uint8_t *image = 0;
    if (boot_store_record(&store, BOOT_RECOVERY_RECORD, &recovery) && recovery.slot == 2 &&
        boot_store_image(&store, &recovery, &image)) {
        store.release(store.context, image);
        boot_efi_start(system, handle, loaded->device, root, BOOT_RECOVERY, 1);
    }
    root->close(root);
}
