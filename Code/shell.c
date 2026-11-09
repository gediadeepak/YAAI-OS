#include "console.h"
#include "platform/platform.h"
#include "settings.h"
#include "shell.h"
#include "memory.h"
#include "clock.h"
#include "brand.h"
#include "files.h"

static int command_is(const char *input, const char *expected)
{
    while (*input == ' ') ++input;
    while (*expected) {
        char character = *input++;
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != *expected++) return 0;
    }
    while (*input == ' ') ++input;
    return *input == 0;
}

static void show_memory(void)
{
    MemoryStats stats = memory_stats();
    console_write("MEMORY / 4 KIB PAGES\n");
    console_write("TRACKED RAM     ");
    console_decimal((stats.usable_pages + stats.retained_pages + stats.limited_pages) * 4);
    console_write(" KIB\nIN USE          ");
    console_decimal((stats.retained_pages + stats.allocated_pages) * 4);
    console_write(" KIB\nFREE RAM        "); console_decimal(stats.free_pages * 4);
    console_write(" KIB\nBOOT RESERVED   "); console_decimal(stats.retained_pages * 4);
    console_write(" KIB\nPAGE ALLOCATED  "); console_decimal(stats.allocated_pages * 4);
    console_write(" KIB\nLIMIT RESERVED  "); console_decimal(stats.limited_pages * 4);
    console_write(" KIB\n");
}

static void show_disk(const SetupConfig *config)
{
    if (!setup_valid(config)) { console_write("LIVE SESSION / NO INSTALLATION SETTINGS\n"); return; }
    console_write("DISK CAPACITY  "); console_decimal(config->disk_sectors / 2048);
    console_write(" MIB\nEFI VOLUME     "); console_decimal(config->esp_sectors / 2048);
    console_write(" MIB\nDATA VOLUME    "); console_decimal(config->data_sectors / 2048);
    console_write(" MIB\nUNALLOCATED    "); console_decimal((config->disk_sectors - config->allocated_sectors) / 2048);
    console_write(" MIB\n");
}

static const char *argument(const char *command, const char *word)
{
    while (*command == ' ') ++command;
    while (*word) {
        char c = *command++; if (c >= 'A' && c <= 'Z') c += 32;
        if (c != *word++) return 0;
    }
    if (*command != ' ') return 0;
    while (*command == ' ') ++command;
    return command;
}
static void execute(const char *command, const BootInfo *boot)
{
    if (command_is(command, "")) return;
    if (command_is(command, "help")) {
        console_write("HELP  - SHOW COMMANDS\nABOUT - ABOUT YAAI OS\nCLEAR - CLEAR THE CONSOLE\n"
                      "MEM   - SHOW MEMORY USAGE\nDISK  - SHOW INSTALLED DISK LAYOUT\nDATE  - SHOW LOCAL DATE AND TIME\n"
                      "FILES - LIST DOCUMENTS\nCAT name.txt - READ A DOCUMENT\nF4 OPENS FILES AND NOTES; F6 SAVES\n");
    } else if (command_is(command, "about")) {
        console_write(OS_NAME " - VERSION I\n" OS_TAGLINE "\nPLATFORM: ");
        console_write(platform_name()); console_write("\n");
    } else if (command_is(command, "clear")) {
        console_clear();
    } else if (command_is(command, "mem")) {
        show_memory();
    } else if (command_is(command, "disk")) {
        show_disk(&boot->setup);
    } else if (command_is(command, "date")) {
        clock_print(settings_current()->zone);
    } else if (command_is(command,"files")) {
        FileEntry entries[FILE_COUNT]; unsigned count = 0;
        FileResult result = files_list(entries,FILE_COUNT,&count);
        if (result != FILE_OK) { console_write(files_error(result)); console_write("\n"); }
        else {
            console_write("DOCUMENTS\n");
            for (unsigned i = 0; i < count; ++i) {
                console_write(entries[i].name);
                if(entries[i].flags&FILE_DIRECTORY)console_write("/ [FOLDER]\n");
                else{console_write("  ");console_decimal(entries[i].size);console_write(" BYTES\n");}
            }
            if (!count) console_write("NO DOCUMENTS YET. PRESS F4 TO CREATE ONE.\n");
        }
    } else if (argument(command,"cat")) {
        static char bytes[FILE_BYTES + 1]; unsigned size = FILE_BYTES;
        FileResult result = files_read(argument(command,"cat"),bytes,&size);
        if (result != FILE_OK) console_write(files_error(result));
        else {
            for (unsigned i = 0; i < size; ++i) if (bytes[i] != '\n' && (bytes[i] < 32 || bytes[i] > 126)) bytes[i] = '?';
            bytes[size] = 0; console_write(bytes);
        }
        console_write("\n");
    } else {
        console_write("UNKNOWN COMMAND. TYPE HELP.\n");
    }
}

static const BootInfo *active_boot;
static char command[128];
static unsigned length, limit;

void shell_begin(const BootInfo *boot)
{
    active_boot = boot;
    console_clear();
    console_write("WELCOME TO YAAI OS\n\n");
    show_memory();
    console_write("\n");
    if (setup_valid(&boot->setup)) show_disk(&boot->setup);
    clock_print(settings_current()->zone);
    console_write("\nTYPE HELP TO SEE THE COMMANDS.\n\n");
    length = 0;
    // Keep input on one line so Backspace cannot reach earlier output.
    limit = console_columns() > 9 ? console_columns() - 9 : 0;
    if (limit > sizeof(command) - 1) limit = sizeof(command) - 1;
    console_write("YAAI > ");
    console_cursor(1);
}

void shell_key(char character)
{
    if (!character || !active_boot) return;
    console_cursor(0);
    if (character == '\n') {
        command[length] = 0;
        console_write("\n");
        execute(command, active_boot);
        length = 0;
        console_write("YAAI > ");
    } else if (character == '\b') {
        if (length) { --length; console_backspace(); }
    } else if (character >= 32 && character <= 126 && length < limit) {
        command[length++] = character;
        char echo[2] = {character, 0};
        console_write(echo);
    }
    console_cursor(1);
}

_Noreturn void shell_run(const BootInfo *boot)
{
    if (!platform_input_initialize(boot->width,boot->height)) {
        console_write("PLATFORM INPUT INITIALIZATION FAILED.\n");
        platform_halt();
    }
    shell_begin(boot);
    for (;;) {
        shell_key(platform_key_read());
        platform_idle();
    }
}
