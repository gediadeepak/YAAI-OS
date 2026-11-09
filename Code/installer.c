#include <stddef.h>
#include "installer.h"
#include "install_disk.h"
#include "memory.h"
#include "clock.h"
#include "brand.h"
#include "boot_efi.h"

static const Guid loaded_image_guid = {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
static const Guid filesystem_guid = {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static const Guid block_guid = {0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static const Guid path_guid = {0x09576e91,0x6d3f,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
static SystemTable *firmware;
static File *installed_root;
static void *installed_device;
_Static_assert(offsetof(BootServices,connect_controller)==264,"UEFI ConnectController ABI");

static void print(const char *text)
{
    uint16_t chunk[128];
    while (*text) {
        unsigned count = 0;
        while (*text && count < 126) {
            if (*text == '\n') chunk[count++] = '\r';
            chunk[count++] = (uint8_t)*text++;
        }
        chunk[count] = 0;
        firmware->console_out->output_string(firmware->console_out, chunk);
    }
}

static void number(uint64_t value)
{
    char digits[21];
    unsigned index = 20;
    digits[index] = 0;
    do { digits[--index] = (char)('0' + value % 10); value /= 10; } while (value);
    print(digits + index);
}

static void page(const char *title)
{
    firmware->console_out->set_attribute(firmware->console_out, 11);
    firmware->console_out->clear_screen(firmware->console_out);
    print(OS_NAME " / INSTALLATION\n" OS_TAGLINE "\n\n");
    firmware->console_out->set_attribute(firmware->console_out, 7);
    print(title); print("\n\n");
}

static void input(char *buffer, unsigned capacity)
{
    unsigned length = 0;
    for (;;) {
        InputKey key;
        if (firmware->console_in->read_key(firmware->console_in, &key) != 0) {
            firmware->boot_services->stall(1000);
            continue;
        }
        if (key.unicode == 13) { buffer[length] = 0; print("\n"); return; }
        if (key.unicode == 8 && length) { --length; print("\b \b"); }
        else if (key.unicode >= 32 && key.unicode <= 126 && length + 1 < capacity) {
            buffer[length++] = (char)key.unicode;
            char echo[2] = {(char)key.unicode, 0}; print(echo);
        }
    }
}

static int equals(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static uint64_t choose_number(uint64_t minimum, uint64_t maximum)
{
    char answer[24];
    for (;;) {
        print("> "); input(answer, sizeof(answer));
        uint64_t value = 0;
        int valid = answer[0] != 0;
        for (unsigned index = 0; answer[index]; ++index) {
            if (answer[index] < '0' || answer[index] > '9' || value > (UINT64_MAX - 9) / 10) {
                valid = 0; break;
            }
            value = value * 10 + answer[index] - '0';
        }
        if (valid && value >= minimum && value <= maximum) return value;
        print("Choose a value from "); number(minimum); print(" to "); number(maximum); print(".\n");
    }
}

static int read_file(File *root, const uint16_t *path, uint8_t **bytes, uint64_t *size)
{
    File *file = 0;
    if (root->open(root, &file, path, 1, 0) != 0) return 0;
    int success = 0;
    if (file->set_position(file, UINT64_MAX) == 0 && file->get_position(file, size) == 0 &&
        *size > 0 && *size <= 32 * 1024 * 1024 && file->set_position(file, 0) == 0 &&
        firmware->boot_services->allocate_pool(2, *size, (void **)bytes) == 0) {
        uint64_t read_size = *size;
        success = file->read(file, &read_size, *bytes) == 0 && read_size == *size;
        if (!success) { firmware->boot_services->free_pool(*bytes); *bytes = 0; }
    }
    file->close(file);
    return success;
}

static uint64_t path_length(const uint8_t *path)
{
    for (uint64_t offset = 0; offset <= 4092;) {
        uint16_t length = (uint16_t)(path[offset + 2] | (path[offset + 3] << 8));
        if (length < 4 || length > 4096 - offset) return 0;
        if (path[offset] == 0x7f && path[offset + 1] == 0xff) return offset;
        offset += length;
    }
    return 0;
}

static int source_disk(void *handle, const uint8_t *source_path, uint64_t source_length)
{
    uint8_t *candidate = 0;
    if (firmware->boot_services->handle_protocol(handle, &path_guid, (void **)&candidate) != 0) return 1;
    uint64_t length = path_length(candidate);
    if (!length) return 1;
    if (length > source_length) return 0;
    for (uint64_t byte = 0; byte < length; ++byte) {
        if (candidate[byte] != source_path[byte]) return 0;
    }
    return 1;
}

static uint64_t detected_ram_mib(void)
{
    uint64_t size = 0, key = 0, stride = 0;
    uint32_t version = 0;
    BootServices *services = firmware->boot_services;
    if (services->get_memory_map(&size, 0, &key, &stride, &version) != 0x8000000000000005ULL ||
        stride < sizeof(MemoryDescriptor)) return 0;
    size += 64 * stride;
    void *map = 0;
    if (services->allocate_pool(2, size, &map) != 0) return 0;
    uint64_t pages = 0;
    if (services->get_memory_map(&size, map, &key, &stride, &version) == 0) {
        for (uint64_t offset = 0; offset < size; offset += stride) {
            const MemoryDescriptor *entry = (const void *)((const uint8_t *)map + offset);
            if (entry->type >= 1 && entry->type <= 10) pages += entry->page_count;
        }
    }
    services->free_pool(map);
    return pages / 256;
}

static void print_two(int value)
{
    if (value < 10) print("0");
    number((uint64_t)value);
}

static int load_settings(File *root, SetupConfig *config)
{
    uint8_t *settings = 0;
    uint64_t size = 0;
    if (!read_file(root, u"\\EFI\\SHRI\\SETUP.CFG", &settings, &size)) return 0;
    int valid = size == sizeof(*config) && setup_valid((const void *)settings);
    if (valid) {
        for (unsigned byte = 0; byte < sizeof(*config); ++byte) ((uint8_t *)config)[byte] = settings[byte];
    }
    firmware->boot_services->free_pool(settings);
    return valid;
}

static int installed_settings(void *source_device, SetupConfig *config)
{
    void **handles = 0;
    uint64_t count = 0;
    BootServices *services = firmware->boot_services;
    // Firmware may connect only the installer disk before starting our image.
    // Connect other block devices recursively so their ESP filesystems appear.
    if(services->connect_controller&&services->locate_handles(2,&block_guid,0,&count,&handles)==0){
        for(uint64_t i=0;i<count;++i)services->connect_controller(handles[i],0,0,1);
        services->free_pool(handles);handles=0;count=0;
    }
    if (services->locate_handles(2, &filesystem_guid, 0, &count, &handles) != 0) return 0;
    int found = 0;installed_root=0;installed_device=0;
    for (uint64_t index = 0; index < count; ++index) {
        if (handles[index] == source_device) continue;
        FileSystem *filesystem = 0;
        File *root = 0;
        if (services->handle_protocol(handles[index], &filesystem_guid, (void **)&filesystem) == 0 &&
            filesystem->open_volume(filesystem, &root) == 0) {
            SetupConfig candidate;
            if(load_settings(root,&candidate)){
                ++found;
                if(found==1){installed_root=root;installed_device=handles[index];*config=candidate;}
                else root->close(root);
            }else root->close(root);
        }
    }
    services->free_pool(handles);
    if(found>1){installed_root->close(installed_root);installed_root=0;}
    return found;
}

uint64_t installer_run(void *image_handle, SystemTable *system, SetupConfig *config)
{
    firmware = system;
    BootServices *services = system->boot_services;
    LoadedImage *loaded = 0;
    FileSystem *filesystem = 0;
    File *root = 0;
    uint8_t *source_path = 0;
    if (services->handle_protocol(image_handle, &loaded_image_guid, (void **)&loaded) != 0 ||
        services->handle_protocol(loaded->device, &filesystem_guid, (void **)&filesystem) != 0 ||
        filesystem->open_volume(filesystem, &root) != 0) return 0; // Live boot remains available.

    // Development boots use the latest EFI build and reuse the installed disk's
    // settings. A standalone installed boot reads its own settings first.
    if (load_settings(root, config)) {
        print("Installed system found. Starting YAAI OS.\n");
        root->close(root);
        return 0;
    }
    int installed=installed_settings(loaded->device,config);
    if(installed>1){
        print("Several installed systems were found. Attach only one system disk before updating.\n");
        root->close(root);return UINT64_C(0x8000000000000015);
    }
    if(installed==1){
        EfiBootStore context;BootStore store=boot_efi_store(&context,installed_root,services);
        for(;;){
            page("EXISTING INSTALLATION");
            print("Your installed system and documents are preserved.\n\n"
                  "1  UPDATE INSTALLED YAAI OS\n2  START INSTALLED SYSTEM\n"
                  "3  ROLL BACK TO PREVIOUS VERSION\n4  TRY THIS BUILD WITHOUT UPDATING\n");
            unsigned choice=(unsigned)choose_number(1,4);
            if(choice==4){installed_root->close(installed_root);root->close(root);return 0;}
            if(choice==1||choice==3){
                uint8_t *image=0;uint64_t image_size=0;
                if(choice==1&&!read_file(root,u"\\EFI\\BOOT\\BOOTX64.EFI",&image,&image_size)){
                    print("Cannot read the update image. No installed files were changed.\n");continue;
                }
                print("Checking and saving boot files. Do not power off during this step...\n");
                UpdateResult result=boot_store_update(&store,image,image_size,choice==3);
                if(image)services->free_pool(image);
                print(boot_update_message(result));print("\n");
                if(result!=UPDATE_OK&&result!=UPDATE_UNCHANGED){char answer[8];print("Press Enter to return to the menu.\n");input(answer,sizeof(answer));continue;}
                print("Booting the installed disk. After shutdown, detach the installer VDI.\n");
            }
            uint64_t status=boot_efi_start(system,image_handle,installed_device,installed_root,BOOT_MAIN,0);
            if(!status){installed_root->close(installed_root);root->close(root);return 0;}
            print("The installed boot image could not start. You can retry the update or roll back.\n");
        }
    }

    page("WELCOME / FIRST START");
    print("Install YAAI OS on a blank virtual disk, or try the live system.\n\n"
          "1  INSTALL YAAI OS\n2  TRY WITHOUT INSTALLING\n");
    if (choose_number(1, 2) == 2) { root->close(root); return 0; }
    if (services->handle_protocol(loaded->device, &path_guid, (void **)&source_path) != 0 || !path_length(source_path)) {
        print("Cannot identify the source disk. Press Enter for the live system.\n");
        char answer[8]; input(answer, sizeof(answer)); root->close(root); return 0;
    }

    BlockIO *disks[8];
    unsigned disk_count = 0;
    void **handles = 0;
    uint64_t handle_count = 0;
    page("STEP 1 / SELECT A BLANK DISK");
    print("Supported targets: blank 512-byte-sector virtual disks, 256 MiB to 32 GiB.\n"
          "The installer source disk is excluded. Existing data is protected.\n\n");
    if (services->locate_handles(2, &block_guid, 0, &handle_count, &handles) == 0) {
        for (uint64_t index = 0; index < handle_count && disk_count < 8; ++index) {
            BlockIO *disk = 0;
            if (source_disk(handles[index], source_path, path_length(source_path)) ||
                services->handle_protocol(handles[index], &block_guid, (void **)&disk) != 0) continue;
            BlockMedia *media = disk->media;
            if (!media->present || media->logical_partition || media->read_only || media->block_size != 512 ||
                media->io_align > 4096 || media->last_block < 524287 || media->last_block >= 67108864) continue;
            disks[disk_count++] = disk;
            number(disk_count); print("  DISK "); number(disk_count); print(" / ");
            number((media->last_block + 1) / 2048); print(" MiB\n");
        }
        services->free_pool(handles);
    }
    if (!disk_count) {
        print("\nNo supported target disk. Attach a new blank virtual disk, then restart.\n"
              "Press Enter to continue to the live system.\n");
        char answer[8]; input(answer, sizeof(answer)); root->close(root); return 0;
    }
    unsigned selected_disk = (unsigned)choose_number(1, disk_count);
    BlockIO *target = disks[selected_disk - 1];
    print("Checking every sector for existing data. This can take a little time...\n");
    int state = install_disk_state(target);
    if (!state) {
        print("This disk has existing data or could not be read. It will not be changed.\n"
              "Press Enter to continue to the live system.\n");
        char answer[8]; input(answer, sizeof(answer)); root->close(root); return 0;
    }
    if (state == 2) print("An incomplete YAAI installation was found. Setup can retry it.\n");

    page("STEP 2 / DISK SPACE");
    uint64_t disk_mib = (target->media->last_block + 1) / 2048;
    print("Disk capacity: "); number(disk_mib); print(" MiB\n"
          "Choose total installation space in MiB, from 256 to "); number(disk_mib);
    print(".\n128 MiB is used for EFI. The remainder becomes a YAAIFS documents volume.\n"
          "Any space beyond your choice stays unallocated.\n");
    uint64_t selected_mib = choose_number(256, disk_mib);
    config->allocated_sectors = selected_mib * 2048;

    page("STEP 3 / MEMORY");
    print("RAM reported by firmware: "); number(detected_ram_mib()); print(" MiB\n");
    print("The VM provides physical RAM. Set an OS usage limit or use all available RAM.\n"
          "A limit above detected RAM uses all available RAM.\n"
          "Enter 0 for all available RAM, or a limit in MiB (minimum 128).\n");
    for (;;) {
        config->ram_limit_mib = choose_number(0, 32768);
        if (config->ram_limit_mib == 0 || config->ram_limit_mib >= 128) break;
        print("Use 0 or at least 128 MiB.\n");
    }

    page("STEP 4 / COUNTRY AND TIME ZONE");
    print("1  INDIA / IST\n2  UNITED KINGDOM / LONDON\n3  USA / NEW YORK\n"
          "4  USA / CHICAGO\n5  USA / DENVER\n6  USA / LOS ANGELES\n7  UTC\n\n"
          "The hardware clock must use UTC. Country rules adjust local date and time.\n");
    uint32_t zone = (uint32_t)choose_number(1, 7);
    config->zone = zone == 7 ? 0 : zone;

    page("STEP 5 / REVIEW AND INSTALL");
    print("Selected disk: DISK "); number(selected_disk); print(" / "); number(disk_mib);
    print(" MiB\nInstallation space: "); number(selected_mib);
    print(" MiB\nEFI partition: 128 MiB\nData partition: ");
    number((selected_mib * 2048 - 34 - 264192) / 2048);
    print(" MiB\nUnallocated: "); number(disk_mib - selected_mib);
    print(" MiB\nRAM limit: ");
    if (config->ram_limit_mib) { number(config->ram_limit_mib); print(" MiB\n"); }
    else print("ALL AVAILABLE\n");
    print("Country / time zone: "); print(setup_zone_name(config->zone));
    DateTime local;
    if (clock_local_time(config->zone, &local)) {
        print("\nLocal date / time: "); number((uint64_t)local.year); print("-");
        print_two(local.month); print("-"); print_two(local.day); print(" ");
        print_two(local.hour); print(":"); print_two(local.minute);
    }
    print("\n\nSetup will create GPT, a FAT32 EFI volume and a YAAIFS documents volume.\n"
          "Type INSTALL to confirm, or anything else to cancel.\n> ");
    char confirmation[24]; input(confirmation, sizeof(confirmation));
    if (!equals(confirmation, "INSTALL")) {
        print("Installation cancelled. Starting the live system.\n");
        for (unsigned byte = 0; byte < sizeof(*config); ++byte) ((uint8_t *)config)[byte] = 0;
        root->close(root); return 0;
    }

    uint8_t *image = 0;
    uint64_t image_size = 0;
    if (!read_file(root, u"\\EFI\\BOOT\\BOOTX64.EFI", &image, &image_size)) {
        print("Cannot read the original EFI executable. The target is unchanged.\n");
        root->close(root); return 0x8000000000000007ULL;
    }
    root->close(root);
    print("\nFormatting and copying YAAI OS...\n");
    int success = install_disk_write(target, image, image_size, config);
    services->free_pool(image);
    if (!success) {
        print("Installation failed. Restart from the installer to retry the marked disk.\n");
        return 0x8000000000000007ULL;
    }
    print("Installation complete. Settings saved.\n"
          "YAAI OS will now start. Next time, boot the installed disk first.\n"
          "Press Enter to continue.\n");
    input(confirmation, sizeof(confirmation));
    return 0;
}
