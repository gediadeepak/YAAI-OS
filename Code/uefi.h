#ifndef UEFI_H
#define UEFI_H
#include <stdint.h>
typedef struct TextOutput TextOutput;

struct TextOutput {
    uint64_t (*reset)(TextOutput *self, uint8_t extended_verification);
    uint64_t (*output_string)(TextOutput *self, const uint16_t *text);
    void (*test_string)(void);
    void (*query_mode)(void);
    void (*set_mode)(void);
    uint64_t (*set_attribute)(TextOutput *, uint64_t);
    uint64_t (*clear_screen)(TextOutput *);
    void (*set_cursor)(void);
    void (*enable_cursor)(void);
};

typedef struct { uint16_t scan_code, unicode; } InputKey;
typedef struct TextInput TextInput;
struct TextInput {
    uint64_t (*reset)(TextInput *, uint8_t);
    uint64_t (*read_key)(TextInput *, InputKey *);
    void *wait_for_key;
};

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} TableHeader;

typedef void (*UnusedService)(void);
typedef struct {
    uint32_t data1;
    uint16_t data2, data3;
    uint8_t data4[8];
} Guid;

// Preserve the UEFI service table's function ordering.
typedef struct {
    TableHeader header;
    UnusedService raise_tpl, restore_tpl;
    uint64_t (*allocate_pages)(uint32_t, uint32_t, uint64_t, uint64_t *);
    UnusedService free_pages;
    uint64_t (*get_memory_map)(uint64_t *, void *, uint64_t *, uint64_t *, uint32_t *);
    uint64_t (*allocate_pool)(uint32_t, uint64_t, void **);
    uint64_t (*free_pool)(void *);
    UnusedService before_handle[9];
    uint64_t (*handle_protocol)(void *, const Guid *, void **);
    UnusedService reserved,register_notify,locate_handle,locate_device_path,install_configuration;
    uint64_t (*load_image)(uint8_t,void *,void *,void *,uint64_t,void **);
    uint64_t (*start_image)(void *,uint64_t *,uint16_t **);
    UnusedService exit_image,unload_image;
    uint64_t (*exit_boot_services)(void *, uint64_t);
    UnusedService monotonic_count;
    uint64_t (*stall)(uint64_t);
    UnusedService watchdog;
    uint64_t (*connect_controller)(void *, void **, void *, uint8_t);
    UnusedService before_locate[5];
    uint64_t (*locate_handles)(uint32_t, const Guid *, void *, uint64_t *, void ***);
    uint64_t (*locate_protocol)(const Guid *, void *, void **);
} BootServices;

typedef struct {
    uint32_t version, width, height, pixel_format;
    uint32_t pixel_masks[4];
    uint32_t pixels_per_scanline;
} GraphicsInfo;

typedef struct {
    uint32_t max_mode, mode;
    GraphicsInfo *info;
    uint64_t info_size, framebuffer_base, framebuffer_size;
} GraphicsMode;

typedef struct GraphicsOutput GraphicsOutput;
struct GraphicsOutput {
    uint64_t (*query_mode)(GraphicsOutput *, uint32_t, uint64_t *, GraphicsInfo **);
    uint64_t (*set_mode)(GraphicsOutput *, uint32_t);
    UnusedService blt;
    GraphicsMode *mode;
};

typedef struct {
    TableHeader header;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    void *console_in_handle;
    TextInput *console_in;
    void *console_out_handle;
    TextOutput *console_out;
    void *standard_error_handle, *standard_error, *runtime_services;
    BootServices *boot_services;
} SystemTable;

typedef struct File File;
struct File {
    uint64_t revision;
    uint64_t (*open)(File *, File **, const uint16_t *, uint64_t, uint64_t);
    uint64_t (*close)(File *);
    UnusedService delete_file;
    uint64_t (*read)(File *, uint64_t *, void *);
    uint64_t (*write)(File *, uint64_t *, const void *);
    uint64_t (*get_position)(File *, uint64_t *);
    uint64_t (*set_position)(File *, uint64_t);
    uint64_t (*get_info)(File *,const Guid *,uint64_t *,void *);
    uint64_t (*set_info)(File *,const Guid *,uint64_t,const void *);
    uint64_t (*flush)(File *);
};

typedef struct FileSystem FileSystem;
struct FileSystem {
    uint64_t revision;
    uint64_t (*open_volume)(FileSystem *, File **);
};

typedef struct {
    uint32_t revision;
    void *parent, *system_table, *device, *file_path, *reserved;
    uint32_t load_options_size;
    void *load_options, *image_base;
    uint64_t image_size;
} LoadedImage;

typedef struct {
    uint32_t id;
    uint8_t removable, present, logical_partition, read_only, write_caching;
    uint32_t block_size, io_align;
    uint64_t last_block;
} BlockMedia;

typedef struct BlockIO BlockIO;
struct BlockIO {
    uint64_t revision;
    BlockMedia *media;
    uint64_t (*reset)(BlockIO *, uint8_t);
    uint64_t (*read)(BlockIO *, uint32_t, uint64_t, uint64_t, void *);
    uint64_t (*write)(BlockIO *, uint32_t, uint64_t, uint64_t, const void *);
    uint64_t (*flush)(BlockIO *);
};


#endif
