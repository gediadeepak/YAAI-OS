#include "settings.h"
#include "storage.h"
#include "screen.h"
static DesktopSettings current = {0,100,0};
static const char *status_text = "Default settings";
const DesktopSettings *settings_current(void) { return &current; }
const char *settings_status(void) { return status_text; }
void settings_initialize(const BootInfo *boot)
{
    current = (DesktopSettings){0,100,boot->setup.zone < 7 ? boot->setup.zone : 0};
    uint32_t data[4]; unsigned size = sizeof(data);
    StoreResult result = storage_get("Desktop",data,&size);
    if (result == STORE_OK && size == sizeof(data) && data[0] == 1 && data[1] < 3 && data[2] <= 100 && data[3] < 7) {
        current = (DesktopSettings){data[1],data[2],data[3]}; status_text = "Settings restored";
        serial_write("SETTINGS: restored\r\n");
    } else {
        status_text = result == STORE_NOT_FOUND ? "Defaults - changes save automatically" : "Defaults - saved settings unavailable";
        serial_write("SETTINGS: using defaults\r\n");
    }
}
static void save(void)
{
    uint32_t data[4] = {1,current.wallpaper,current.volume,current.zone};
    StoreResult result = storage_put("Desktop",data,sizeof(data));
    status_text = result == STORE_OK ? "Settings saved" : "Changes not saved - session only";
    serial_write(result == STORE_OK ? "SETTINGS: saved\r\n" : "SETTINGS: save failed / session only\r\n");
}
void settings_wallpaper(unsigned value) { if (value < 3 && value != current.wallpaper) { current.wallpaper = value; save(); } }
void settings_volume(unsigned value) { if (value <= 100 && value != current.volume) { current.volume = value; save(); } }
void settings_zone(unsigned value) { if (value < 7 && value != current.zone) { current.zone = value; save(); } }
