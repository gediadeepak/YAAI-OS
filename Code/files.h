#ifndef YAAI_FILES_H
#define YAAI_FILES_H
#include "block.h"
enum {
    FILE_COUNT = 64,
    FILE_BYTES = 65536,
    FILE_NAME = 32,
    FILE_PATH = 256,
    FILE_V2_BANK = 129,
    FILE_V2_SECTORS = 2 + FILE_COUNT * 2 * FILE_V2_BANK
};
typedef enum {
    FILE_OK,
    FILE_NOT_FOUND,
    FILE_UNAVAILABLE,
    FILE_READ_ONLY,
    FILE_INVALID,
    FILE_TOO_LARGE,
    FILE_FULL,
    FILE_IO_ERROR,
    FILE_CORRUPT,
    FILE_EXISTS,
    FILE_NOT_DIRECTORY,
    FILE_NOT_EMPTY,
    FILE_BAD_MOVE
} FileResult;
typedef struct {
    char name[FILE_NAME];
    unsigned size, id, parent, flags;
} FileEntry;
enum { FILE_DIRECTORY = 1, FILE_TRASHED = 2, FILE_DELETED = 4 };
typedef struct {
    unsigned version, entries, folders, trashed, used_bytes, max_bytes;
    uint64_t volume_sectors;
} FileUsage;
void files_initialize(void);
FileResult files_sync(void);
FileResult files_mount(const BlockDevice *device);
FileResult files_list(FileEntry *entries, unsigned capacity, unsigned *count);
FileResult files_read(const char *name, void *data, unsigned *size);
FileResult files_write(const char *name, const void *data, unsigned size);
FileResult files_upgrade(void);
FileResult files_list_directory(unsigned parent, int trash, FileEntry *entries, unsigned capacity,
                                unsigned *count);
FileResult files_write_at(unsigned parent, const char *name, const void *data, unsigned size);
FileResult files_read_id(unsigned id, void *data, unsigned *size);
FileResult files_mkdir(unsigned parent, const char *name);
FileResult files_rename(unsigned id, const char *name);
FileResult files_move(unsigned id, unsigned parent);
FileResult files_trash(unsigned id);
FileResult files_restore(unsigned id);
FileResult files_remove(unsigned id);
FileResult files_entry(unsigned id, FileEntry *entry);
FileResult files_resolve(const char *path, unsigned *id);
FileResult files_path(unsigned id, char *path, unsigned capacity);
FileResult files_usage(FileUsage *usage);
const char *files_status(void);
const char *files_error(FileResult result);
#endif
