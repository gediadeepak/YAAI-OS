#include "settings.h"
#include "screen.h"
#include "storage.h"
static DesktopSettings current = {0, 100, 0, 0, 0};
static const char *status_text = "Default settings";
const DesktopSettings *settings_current(void) { return &current; }
const char *settings_status(void) { return status_text; }
void settings_initialize(const BootInfo *boot) {
    current = (DesktopSettings){0, 100, boot->setup.zone < 7 ? boot->setup.zone : 0, 0, 0};
    uint32_t data[4];
    unsigned size = sizeof(data);
    StoreResult result = storage_get("Desktop", data, &size);
    if (result == STORE_OK && size == sizeof(data) && data[0] == 1 && data[1] < 3 && data[2] <= 100 &&
        data[3] < 7) {
        current = (DesktopSettings){data[1], data[2], data[3], 0, 0};
        status_text = "Settings restored";
        serial_write("SETTINGS: restored\r\n");
    } else {
        status_text = result == STORE_NOT_FOUND ? "Defaults - changes save automatically"
                                                : "Defaults - saved settings unavailable";
        serial_write("SETTINGS: using defaults\r\n");
    }
    uint32_t theme[2];
    size = sizeof(theme);
    if (storage_get("Theme", theme, &size) == STORE_OK && size == sizeof(theme) && theme[0] == 1 &&
        theme[1] < 2) {
        current.theme = theme[1];
        status_text = "Settings restored";
    }
    uint32_t mode[2];
    size = sizeof(mode);
    if (storage_get("Display", mode, &size) == STORE_OK && size == sizeof(mode) && mode[0] == 1 &&
        mode[1] < 4) {
        current.resolution = mode[1];
        status_text = "Settings restored";
    }
}
static void save(void) {
    uint32_t data[4] = {1, current.wallpaper, current.volume, current.zone};
    StoreResult result = storage_put("Desktop", data, sizeof(data));
    status_text = result == STORE_OK ? "Settings saved" : "Changes not saved - session only";
    serial_write(result == STORE_OK ? "SETTINGS: saved\r\n" : "SETTINGS: save failed / session only\r\n");
}
void settings_wallpaper(unsigned value) {
    if (value < 3 && value != current.wallpaper) {
        current.wallpaper = value;
        save();
    }
}
void settings_volume(unsigned value) {
    if (value <= 100 && value != current.volume) {
        current.volume = value;
        save();
    }
}
void settings_zone(unsigned value) {
    if (value < 7 && value != current.zone) {
        current.zone = value;
        save();
    }
}

void settings_theme(unsigned value) {
    if (value >= 2 || value == current.theme)
        return;
    current.theme = value;
    uint32_t data[2] = {1, value};
    status_text = storage_put("Theme", data, sizeof(data)) == STORE_OK ? "Settings saved"
                                                                       : "Changes not saved - session only";
}
void settings_resolution(unsigned value) {
    if (value >= 4 || value == current.resolution)
        return;
    uint32_t data[2] = {1, value};
    StoreResult result = storage_put("Display", data, sizeof(data));
    if (result == STORE_OK)
        current.resolution = value;
    status_text =
        result == STORE_OK ? "Resolution saved - restart to apply" : "Resolution not saved - try again";
}