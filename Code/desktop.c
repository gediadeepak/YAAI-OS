#include "platform/platform.h"
#include "desktop.h"
#include "screen.h"
#include "console.h"



#include "shell.h"
#include "memory.h"
#include "clock.h"
#include "gui.h"
#include "brand.h"
#include "settings.h"
#include "file_app.h"
#include "power.h"
#include "apps.h"
#include "desktop_keys.h"




typedef GuiRect Rect;
static const BootInfo *display;
static BootInfo canvas, wallpaper;
static WindowManager wm;
#define terminal_window wm.rect[1]
#define system_window wm.rect[2]
#define files_window wm.rect[3]
#define about_window wm.rect[4]
#define calculator_window wm.rect[5]
#define active_window wm.active
#define visible wm.visible
#define order wm.order
static Rect dock, brand_button, terminal_button, system_button, files_button, calculator_button, about_button, clock_panel, menu;
static PlatformPointer pointer;
static int menu_open, terminal_started, scene_dirty, hovered;
static int dragging, resizing, drag_x, drag_y, wallpaper_style, hardware_page;
static unsigned volume = 100;
static Rect context_menu;
static int context_open;
static int power_dialog;
static const char *power_error;
static Rect power_panel;

static Rect *window_rect(int window) { return &wm.rect[window]; }
static void focus(int window)
{
    windows_focus(&wm,(unsigned)window);
}
static void hide_window(int window)
{
    windows_hide(&wm,(unsigned)window);
}
static Rect work_area;
static int fullscreen_window(void) {
    return active_window && visible[active_window] && wm.layout[active_window]==WINDOW_MAXIMIZED;
}
static void reframe_terminal(void) {
    if(terminal_started) console_reframe(terminal_window.x+16,terminal_window.y+64,
                                        terminal_window.width-32,terminal_window.height-80);
}
static void arrange_window(int id,int layout) {
    if(!id)return;
    const AppDefinition *app=apps_find((unsigned)id);
    int mw=app?app->min_width:id==1?420:544, mh=app?app->min_height:id==1?220:260;
    Rect area=layout==WINDOW_MAXIMIZED?(Rect){0,0,(int)canvas.width,(int)canvas.height}:work_area;
    if(windows_arrange(&wm,(unsigned)id,layout,area,mw,mh)) {
        if(id==1)reframe_terminal();
        scene_dirty=1;
        serial_write("DESKTOP: window layout changed\r\n");
    }
}
static void toggle_maximize(int id) {
    if(id)arrange_window(id,wm.layout[id]==WINDOW_MAXIMIZED?WINDOW_NORMAL:WINDOW_MAXIMIZED);
}
static char time_text[9] = "--:--:--", date_text[11] = "----/--/--";

static uint32_t rgb(unsigned red, unsigned green, unsigned blue)
{
    return color(&canvas,(uint8_t)red,(uint8_t)green,(uint8_t)blue);
}

static int minimum(int left, int right) { return left < right ? left : right; }
static int inside(Rect rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

static void label(int x, int y, const char *message, GuiFont font, uint32_t fill)
{
    gui_text(&canvas,x,y,message,font,fill);
}

static void centered(Rect rect, int y, const char *message, GuiFont font, uint32_t fill)
{
    label(rect.x + (rect.width - gui_text_width(message,font)) / 2,y,message,font,fill);
}

static void number(int x, int y, uint64_t value, const char *suffix, GuiFont font)
{
    char digits[21];
    unsigned first = 20;
    digits[first] = 0;
    do { digits[--first] = (char)('0' + value % 10); value /= 10; } while (value);
    label(x,y,digits + first,font,rgb(236,241,249));
    label(x + gui_text_width(digits + first,font) + 7,y + (font == GUI_HEADING ? 10 : 0),suffix,GUI_SMALL,rgb(143,162,187));
}

static void present(Rect rect)
{
    int left = rect.x < 0 ? 0 : rect.x, top = rect.y < 0 ? 0 : rect.y;
    int right = minimum(rect.x + rect.width,(int)display->width);
    int bottom = minimum(rect.y + rect.height,(int)display->height);
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            display->framebuffer[(uint64_t)y * display->pixels_per_scanline + x] = canvas.framebuffer[(uint64_t)y * canvas.width + x];
}

static void restore_wallpaper(Rect rect)
{
    for (int y = rect.y; y < rect.y + rect.height; ++y)
        for (int x = rect.x; x < rect.x + rect.width; ++x)
            canvas.framebuffer[(uint64_t)y * canvas.width + x] = wallpaper.framebuffer[(uint64_t)y * canvas.width + x];
}

static void draw_pointer(void)
{
    if (!platform_pointer_available()) return;
    static const char *shape[] = {
        "X", "XX", "XoX", "XooX", "XoooX", "XooooX",
        "XoooooX", "XooooooX", "XoooooooX", "XooooooooX",
        "XoooooooooX", "XooooooooooX", "XoooooooooooX",
        "XooooooXXXXXXX", "XoooXooX", "XooXXooX",
        "XoX  XooX", "XX   XooX", "X     XooX",
        "      XooX", "       XooX", "       XooX", "        XX"
    };
    for (unsigned y = 0; y < sizeof(shape) / sizeof(shape[0]); ++y)
        for (int x = 0; shape[y][x]; ++x)
            if (shape[y][x] != ' ')
                pixel(display,pointer.x + x,pointer.y + (int)y,
                      shape[y][x] == 'X' ? rgb(8,18,29) : rgb(239,248,255));
}

static void terminal_symbol(int x, int y)
{
    gui_round_rect(&canvas,(Rect){x,y,42,34},9,rgb(28,58,79),255);
    gui_round_rect(&canvas,(Rect){x + 1,y + 1,40,32},8,rgb(13,28,46),255);
    gui_line(&canvas,x + 10,y + 10,x + 16,y + 16,2,rgb(132,224,218));
    gui_line(&canvas,x + 16,y + 16,x + 10,y + 22,2,rgb(132,224,218));
    gui_line(&canvas,x + 22,y + 23,x + 31,y + 23,2,rgb(132,224,218));
}

static void system_symbol(int x, int y)
{
    for (int row = 0; row < 3; ++row) {
        int top = y + 7 + row * 11;
        gui_line(&canvas,x + 3,top,x + 36,top,2,rgb(149,185,207));
        int knob = x + 9 + row * 8;
        gui_round_rect(&canvas,(Rect){knob,top - 4,8,8},4,rgb(217,188,122),255);
    }
}

static void files_symbol(int x, int y)
{
    gui_round_rect(&canvas,(Rect){x + 2,y + 3,20,12},4,rgb(213,181,112),255);
    gui_round_rect(&canvas,(Rect){x,y + 10,42,27},6,rgb(177,142,81),255);
    gui_round_rect(&canvas,(Rect){x + 2,y + 14,38,21},5,rgb(226,199,143),255);
}
static void brand_symbol(int x, int y)
{
    gui_round_rect(&canvas,(Rect){x,y,36,36},12,rgb(90,88,65),210);
    gui_round_rect(&canvas,(Rect){x + 1,y + 1,34,34},11,rgb(41,44,42),255);
    gui_line(&canvas,x + 10,y + 10,x + 18,y + 18,2,rgb(231,206,150));
    gui_line(&canvas,x + 26,y + 10,x + 18,y + 18,2,rgb(231,206,150));
    gui_line(&canvas,x + 18,y + 18,x + 18,y + 27,2,rgb(231,206,150));
}
static void calculator_symbol(int x,int y)
{
    gui_round_rect(&canvas,(Rect){x+4,y,30,38},7,rgb(128,177,188),255);
    gui_round_rect(&canvas,(Rect){x+8,y+5,22,9},2,rgb(20,40,55),255);
    for(int row=0;row<2;++row)for(int col=0;col<3;++col)
        gui_round_rect(&canvas,(Rect){x+9+col*7,y+20+row*7,4,4},1,rgb(24,48,61),255);
}

static void create_wallpaper(void)
{
    if (!gui_wallpaper(&wallpaper)) {
        for (uint64_t index = 0; index < (uint64_t)canvas.width * canvas.height; ++index) wallpaper.framebuffer[index] = rgb(7,14,30);
    }
    if (wallpaper_style) {
        for (unsigned y = 0; y < canvas.height; ++y)
            for (unsigned x = 0; x < canvas.width; ++x) {
                unsigned glow = (x + y) * 40 / (canvas.width + canvas.height);
                wallpaper.framebuffer[(uint64_t)y * canvas.width + x] = wallpaper_style == 1 ?
                    rgb(8,14 + glow / 2,29 + glow) : rgb(5,24 + glow,35 + glow / 2);
            }
    }
    restore_wallpaper((Rect){0,0,(int)canvas.width,(int)canvas.height});
    for (uint64_t index = 0; index < (uint64_t)canvas.width * canvas.height; ++index) wallpaper.framebuffer[index] = canvas.framebuffer[index];
}

static void refresh_clock(void)
{
    DateTime time;
    if (!clock_local_time(settings_current()->zone,&time)) return;
    time_text[0] = (char)('0' + time.hour / 10); time_text[1] = (char)('0' + time.hour % 10);
    time_text[3] = (char)('0' + time.minute / 10); time_text[4] = (char)('0' + time.minute % 10);
    time_text[6] = (char)('0' + time.second / 10); time_text[7] = (char)('0' + time.second % 10);
    date_text[0] = (char)('0' + time.year / 1000); date_text[1] = (char)('0' + time.year / 100 % 10);
    date_text[2] = (char)('0' + time.year / 10 % 10); date_text[3] = (char)('0' + time.year % 10);
    date_text[5] = (char)('0' + time.month / 10); date_text[6] = (char)('0' + time.month % 10);
    date_text[8] = (char)('0' + time.day / 10); date_text[9] = (char)('0' + time.day % 10);
}

static void clock_widget(int shadow)
{
    if (shadow) gui_shadow(&canvas,clock_panel,18);
    gui_round_rect(&canvas,clock_panel,18,rgb(15,28,46),218);
    int x = clock_panel.x, y = clock_panel.y;
    gui_round_rect(&canvas,(Rect){x + 20,y + 20,22,22},11,rgb(75,116,132),255);
    gui_round_rect(&canvas,(Rect){x + 22,y + 22,18,18},9,rgb(20,39,56),255);
    gui_line(&canvas,x + 31,y + 24,x + 31,y + 31,1,rgb(153,211,217));
    gui_line(&canvas,x + 31,y + 31,x + 36,y + 33,1,rgb(153,211,217));
    label(x + 58,y + 7,time_text,GUI_BODY,rgb(229,238,247));
    label(x + 58,y + 34,date_text,GUI_SMALL,rgb(150,171,194));
}

static void draw_dock(void)
{
    gui_shadow(&canvas,dock,24);
    gui_round_rect(&canvas,dock,24,rgb(19,32,51),235);
    Rect buttons[6] = {brand_button,terminal_button,system_button,files_button,calculator_button,about_button};
    int targets[6] = {1,2,3,9,15,12},ids[6]={0,1,2,3,5,4};
    for (int index = 0; index < 6; ++index) {
        if (hovered == targets[index] || (index && active_window == ids[index]))
            gui_round_rect(&canvas,buttons[index],15,rgb(50,79,97),hovered == targets[index] ? 220 : 155);
    }
    brand_symbol(brand_button.x + 7,brand_button.y + 7);
    terminal_symbol(terminal_button.x + 4,terminal_button.y + 9);
    system_symbol(system_button.x + 5,system_button.y + 7);
    files_symbol(files_button.x + 4,files_button.y + 6);
    calculator_symbol(calculator_button.x+4,calculator_button.y+5);
    gui_round_rect(&canvas,(Rect){about_button.x+12,about_button.y+8,28,28},14,rgb(75,112,135),255);
    label(about_button.x+22,about_button.y+12,"i",GUI_BODY,rgb(227,241,249));
    for(unsigned i=1;i<6;++i)if(wm.opened[ids[i]])
        gui_round_rect(&canvas,(Rect){buttons[i].x+20,buttons[i].y+56,10,3},1,active_window==ids[i]?rgb(142,220,213):rgb(102,125,145),255);
}

static void chrome(int id, Rect window, const char *title)
{
    int maximized=wm.layout[id]==WINDOW_MAXIMIZED;
    if(!maximized)gui_shadow(&canvas,window,20);
    gui_round_rect(&canvas,window,maximized?0:20,rgb(61,81,103),220);
    gui_round_rect(&canvas,(Rect){window.x + 1,window.y + 1,window.width - 2,window.height - 2},maximized?0:19,rgb(14,23,39),250);
    label(window.x + 25,window.y + 15,title,GUI_BODY,rgb(218,232,247));
    gui_line(&canvas,window.x + 20,window.y + 51,window.x + window.width - 21,window.y + 51,1,rgb(36,52,73));
    int x = window.x + window.width - 35, y = window.y + 25;
    gui_round_rect(&canvas,(Rect){x - 12,y - 12,24,24},12,hovered == 100*id+4 ? rgb(143,69,78) : rgb(57,47,64),255);
    gui_line(&canvas,x - 4,y - 4,x + 4,y + 4,1,rgb(236,190,192));
    gui_line(&canvas,x + 4,y - 4,x - 4,y + 4,1,rgb(236,190,192));
    gui_line(&canvas,x - 82,y + 3,x - 70,y + 3,2,rgb(170,196,215));
    int bx=x-40;
    if(hovered==100*id+13)gui_round_rect(&canvas,(Rect){bx-15,y-15,30,30},8,rgb(46,72,92),255);
    if(wm.layout[id]==WINDOW_MAXIMIZED) {
        gui_line(&canvas,bx-3,y-6,bx+6,y-6,1,rgb(170,196,215));
        gui_line(&canvas,bx+6,y-6,bx+6,y+3,1,rgb(170,196,215));
    }
    gui_line(&canvas,bx-6,y-3,bx+3,y-3,1,rgb(170,196,215));
    gui_line(&canvas,bx-6,y-3,bx-6,y+6,1,rgb(170,196,215));
    gui_line(&canvas,bx+3,y-3,bx+3,y+6,1,rgb(170,196,215));
    gui_line(&canvas,bx-6,y+6,bx+3,y+6,1,rgb(170,196,215));
    gui_line(&canvas,window.x + window.width - 22,window.y + window.height - 9,
             window.x + window.width - 9,window.y + window.height - 22,2,rgb(106,146,164));
}

static void metric_card(Rect card, const char *title, uint64_t value, int compact)
{
    gui_round_rect(&canvas,card,14,rgb(31,47,67),225);
    label(card.x + 14,card.y + 10,title,GUI_SMALL,rgb(148,171,194));
    number(card.x + 14,card.y + (compact ? 31 : 42),value,"MiB",GUI_HEADING);
}

static void system_panel(void)
{
    Rect window = system_window;
    chrome(2,window,hardware_page ? "Devices and sound" : "System overview");
    int x = window.x + 24, y = window.y;
    if (hardware_page) {
        PlatformPower power = platform_power();
        const char *messages[] = {
            platform_audio_available() ? "Sound: stereo output ready" : "Sound: no supported output device",
            platform_network_status(),
            platform_wireless_status(),
            "SIM: modem support not implemented",
            !power.supported ? "Power: reporting unavailable" :
                !power.present ? "Power: no battery reported" :
                power.charging ? "Battery: charging" : power.online ? "Battery: plugged in" : "Battery: discharging"
        };
        for (int index = 0; index < 5; ++index)
            label(x,y + 60 + index * 24,messages[index],GUI_SMALL,rgb(190,211,230));
        if (power.percent >= 0) number(x + 300,y + 154,(unsigned)power.percent,"%",GUI_BODY);
        label(x,y + 180,"IPv4:",GUI_SMALL,rgb(151,218,214));
        label(x + 50,y + 180,platform_network_address(),GUI_SMALL,rgb(190,211,230));
        label(x,y + 202,"Volume: click to cycle 100 / 50 / mute",GUI_SMALL,rgb(151,218,214));
        number(x,y + 226,volume,"%",GUI_BODY);
        label(x + 105,y + 226,"Play test sound",GUI_SMALL,rgb(218,203,160));
        return;
    }
    int compact = window.height < 420;
    if (compact) label(x,y + 55,OS_TAGLINE,GUI_SMALL,rgb(185,185,165));
    else {
        label(x,y + 71,OS_NAME,GUI_HEADING,rgb(218,235,240));
        label(x,y + 110,OS_TAGLINE,GUI_SMALL,rgb(185,185,165));
    }
    int card_y = y + (compact ? 85 : 147), card_height = compact ? 72 : 96;
    int card_width = (window.width - 72) / 3;
    MemoryStats stats = memory_stats();
    metric_card((Rect){x,card_y,card_width,card_height},"Total RAM",(stats.usable_pages + stats.retained_pages + stats.limited_pages) / 256,compact);
    metric_card((Rect){x + card_width + 12,card_y,card_width,card_height},"In use",(stats.retained_pages + stats.allocated_pages) / 256,compact);
    metric_card((Rect){x + card_width * 2 + 24,card_y,card_width,card_height},"Available",stats.free_pages / 256,compact);
    int storage_y = y + (compact ? 170 : 267), region_y = y + (compact ? 219 : 345);
    label(x,storage_y,"Storage",GUI_SMALL,rgb(205,184,137));
    if (setup_valid(&display->setup)) {
        number(x,storage_y + 22,display->setup.esp_sectors / 2048,"MiB system",GUI_BODY);
        number(x + window.width / 2 - 24,storage_y + 22,display->setup.data_sectors / 2048,"MiB data",GUI_BODY);
    } else label(x,storage_y + 22,"Live session",GUI_BODY,rgb(192,211,229));
    label(x,region_y,"Country and time zone (click to change)",GUI_SMALL,rgb(205,184,137));
    label(x,region_y + 22,setup_zone_name(settings_current()->zone),GUI_SMALL,rgb(198,217,234));
    if (!compact) {
        label(x,region_y + 53,date_text,GUI_BODY,rgb(170,192,213));
        label(x + 130,region_y + 53,time_text,GUI_BODY,rgb(170,192,213));
    }
}

static void scene(void)
{
    restore_wallpaper((Rect){0,0,(int)canvas.width,(int)canvas.height});
    for (unsigned pass = 0; pass < wm.count; ++pass) {
        int window = order[pass];
        if (window && visible[window]) {
            if (window == 1) { chrome(1,terminal_window,"Terminal"); console_redraw(); }
            else if (window == 2) system_panel();
            else if (apps_find((unsigned)window)) { chrome(window,*window_rect(window),apps_find((unsigned)window)->title);apps_draw((unsigned)window,&canvas,*window_rect(window)); }
            else {
                chrome(4,about_window,"About YAAI OS");
                int x = about_window.x, y = about_window.y;
                int compact = about_window.height < 350;
                if (!compact) brand_symbol(x + about_window.width / 2 - 18,y + 68);
                centered(about_window,y + (compact ? 66 : 120),OS_NAME,GUI_HEADING,rgb(238,241,246));
                centered(about_window,y + (compact ? 112 : 164),OS_TAGLINE,GUI_SMALL,rgb(164,196,200));
                centered(about_window,y + (compact ? 144 : 206),"VERSION I",GUI_SMALL,rgb(218,196,145));
                centered(about_window,y + (compact ? 176 : 246),platform_name(),GUI_SMALL,rgb(164,184,206));
                centered(about_window,y + (compact ? 204 : 280),settings_status(),GUI_SMALL,rgb(164,196,200));
                centered(about_window,y + (compact ? 232 : 322),"F1 Terminal   F2 System   F3 Devices   F4 Files",GUI_SMALL,rgb(151,173,194));
            }
        }
    }
    if(!fullscreen_window()) {clock_widget(1);draw_dock();}
    if (menu_open) {
        gui_shadow(&canvas,menu,20);
        gui_round_rect(&canvas,menu,20,rgb(21,35,54),248);
        label(menu.x + 22,menu.y + 15,"YAAI OS",GUI_BODY,rgb(224,207,167));
        if (hovered == 7) gui_round_rect(&canvas,(Rect){menu.x + 10,menu.y + 52,menu.width - 20,38},12,rgb(46,72,92),235);
        if (hovered == 8) gui_round_rect(&canvas,(Rect){menu.x + 10,menu.y + 92,menu.width - 20,38},12,rgb(46,72,92),235);
        label(menu.x + 22,menu.y + 60,"Open Terminal",GUI_SMALL,rgb(218,232,246));
        label(menu.x + 22,menu.y + 100,"System information",GUI_SMALL,rgb(218,232,246));
        if (hovered == 11) gui_round_rect(&canvas,(Rect){menu.x + 10,menu.y + 132,menu.width - 20,38},12,rgb(46,72,92),235);
        label(menu.x + 22,menu.y + 140,"Files and notes",GUI_SMALL,rgb(218,232,246));
        if (hovered == 12) gui_round_rect(&canvas,(Rect){menu.x + 10,menu.y + 172,menu.width - 20,38},12,rgb(46,72,92),235);
        label(menu.x + 22,menu.y + 180,"About YAAI OS",GUI_SMALL,rgb(218,232,246));
        if(hovered==16)gui_round_rect(&canvas,(Rect){menu.x+10,menu.y+212,menu.width-20,38},12,rgb(46,72,92),235);
        label(menu.x+22,menu.y+220,"Calculator (F11)",GUI_SMALL,rgb(218,232,246));
        const char *power_items[] = {"Restart (R)", "Shut down (S)"};
        for (int i = 0; i < 2; ++i) {
            if (hovered == 13 + i) gui_round_rect(&canvas,(Rect){menu.x + 10,menu.y + 252 + i * 40,menu.width - 20,38},12,rgb(46,72,92),235);
            label(menu.x + 22,menu.y + 260 + i * 40,power_items[i],GUI_SMALL,rgb(218,232,246));
        }
    }
    if (context_open) {
        gui_shadow(&canvas,context_menu,14);
        gui_round_rect(&canvas,context_menu,14,rgb(19,34,53),250);
        const char *items[] = {"AI human wallpaper", "Midnight wallpaper", "Teal wallpaper", "Devices and sound"};
        for (int index = 0; index < 4; ++index) {
            if (hovered == 20 + index)
                gui_round_rect(&canvas,(Rect){context_menu.x + 6,context_menu.y + 6 + index * 36,238,34},8,rgb(44,69,87),255);
            label(context_menu.x + 16,context_menu.y + 15 + index * 36,items[index],GUI_SMALL,rgb(218,232,246));
        }
    }
    if (power_dialog) {
        Rect p = power_panel;
        gui_round_rect(&canvas,(Rect){0,0,(int)canvas.width,(int)canvas.height},0,rgb(3,8,16),150);
        gui_shadow(&canvas,p,20); gui_round_rect(&canvas,p,20,rgb(17,30,47),255);
        label(p.x + 26,p.y + 24,power_dialog == 2 ? "Restart YAAI OS?" : "Shut down YAAI OS?",GUI_HEADING,rgb(233,240,249));
        label(p.x + 26,p.y + 81,file_app_has_changes() ? "Your open note will be saved first." : "Saved files will be flushed to disk.",GUI_SMALL,rgb(168,196,211));
        label(p.x + 26,p.y + 113,power_error ? power_error : "Cancel to keep working. Enter to continue.",GUI_SMALL,power_error ? rgb(244,174,166) : rgb(168,196,211));
        Rect cancel = {p.x + 24,p.y + 168,148,40}, confirm = {p.x + p.width - 196,p.y + 168,172,40};
        gui_round_rect(&canvas,cancel,10,rgb(38,57,77),255);
        gui_round_rect(&canvas,confirm,10,rgb(44,87,96),255);
        centered(cancel,cancel.y + 11,"Cancel",GUI_SMALL,rgb(224,236,244));
        centered(confirm,confirm.y + 11,power_dialog == 2 ? "Restart" : "Shut down",GUI_SMALL,rgb(224,236,244));
    }
}
static void confirm_power(void)
{
    if (!file_app_save_pending()) power_error = "Save failed. Cancel and check Files.";
    else power_error = power_finish(display,power_dialog == 2);
    scene_dirty = 1;
}

static void open_window(int window)
{
    if(apps_find((unsigned)window))apps_launch((unsigned)window);
    else windows_open(&wm,(unsigned)window);
    menu_open = 0;
    context_open = 0;
    if (window == 1 && !terminal_started) {
        console_set_viewport(&canvas,terminal_window.x + 16,terminal_window.y + 64,
                             terminal_window.width - 32,terminal_window.height - 80);
        shell_begin(display);
        terminal_started = 1;
    }
    scene_dirty = 1;
    serial_write(window == 1 ? "DESKTOP: Terminal opened\r\n" : window == 2 ? "DESKTOP: System opened\r\n" : window == 3 ? "DESKTOP: Files opened\r\n" : window == APP_CALCULATOR ? "DESKTOP: Calculator opened\r\n" : "DESKTOP: About opened\r\n");
}

static int target_at(int x, int y)
{
    if (power_dialog) {
        if (inside((Rect){power_panel.x + 24,power_panel.y + 168,148,40},x,y)) return 31;
        if (inside((Rect){power_panel.x + power_panel.width - 196,power_panel.y + 168,172,40},x,y)) return 32;
        return 30;
    }
    if (context_open && inside(context_menu,x,y)) {
        int item = (y - context_menu.y - 6) / 36;
        return item >= 0 && item < 4 ? 20 + item : 0;
    }
    if (menu_open && inside(menu,x,y)) {
        if (inside((Rect){menu.x + 10,menu.y + 52,menu.width - 20,38},x,y)) return 7;
        if (inside((Rect){menu.x + 10,menu.y + 92,menu.width - 20,38},x,y)) return 8;
        if (inside((Rect){menu.x + 10,menu.y + 132,menu.width - 20,38},x,y)) return 11;
        if (inside((Rect){menu.x + 10,menu.y + 172,menu.width - 20,38},x,y)) return 12;
        if (inside((Rect){menu.x + 10,menu.y + 212,menu.width - 20,38},x,y)) return 16;
        if (inside((Rect){menu.x + 10,menu.y + 252,menu.width - 20,38},x,y)) return 13;
        if (inside((Rect){menu.x + 10,menu.y + 292,menu.width - 20,38},x,y)) return 14;
        return 0;
    }
    if(!fullscreen_window()) {
    if (inside(brand_button,x,y)) return 1;
    if (inside(terminal_button,x,y)) return 2;
    if (inside(system_button,x,y)) return 3;
    if (inside(files_button,x,y)) return 9;
    if (inside(calculator_button,x,y)) return 15;
    if (inside(about_button,x,y)) return 12;
    }
    for (int pass = (int)wm.count-1; pass >= 0; --pass) {
        int id = order[pass];
        if (!id || !visible[id]) continue;
        Rect window = *window_rect(id);
        if (!inside(window,x,y)) continue;
        if (inside((Rect){window.x + window.width - 53,window.y + 7,36,36},x,y)) return 100 * id + 4;
        if (inside((Rect){window.x + window.width - 89,window.y + 7,36,36},x,y)) return 100 * id + 13;
        if (inside((Rect){window.x + window.width - 125,window.y + 7,36,36},x,y)) return 100 * id + 9;
        if (x >= window.x + window.width - 25 && y >= window.y + window.height - 25) return 100 * id + 11;
        return 100 * id + (y < window.y + 52 ? 10 : 12);
    }
    return 0;
}

static void click(int x, int y)
{
    int target = target_at(x,y);
    if (power_dialog) {
        if (target == 31) { power_dialog = 0; power_error = 0; scene_dirty = 1; }
        else if (target == 32) confirm_power();
        return;
    }
    if (target >= 20 && target <= 23) {
        context_open = 0;
        if (target == 23) { hardware_page = 1; open_window(2); }
        else { wallpaper_style = target - 20; settings_wallpaper((unsigned)wallpaper_style); create_wallpaper(); scene_dirty = 1; }
        serial_write("DESKTOP: context action\r\n");
        return;
    }
    if (context_open) scene_dirty = 1;
    context_open = 0;
    if (target >= 100) {
        menu_open = 0;
        int id = target / 100, action = target % 100;
        focus(id);
        if (action == 4 || action == 9) {
            if(action==4) {
                if(apps_find((unsigned)id))apps_close((unsigned)id);else windows_close(&wm,(unsigned)id);
            } else hide_window(id);
        } else if(action==13) toggle_maximize(id);
        else if (action == 10 || action == 11) {
            if(wm.layout[id]!=WINDOW_NORMAL) {
                arrange_window(id,WINDOW_NORMAL);
                Rect *restored=window_rect(id);
                if(action==10) {
                    restored->x=minimum(x-restored->width/2,(int)canvas.width-restored->width);
                    restored->y=minimum(y-25,(int)canvas.height-100-restored->height);
                    if(restored->x<0)restored->x=0;
                    if(restored->y<0)restored->y=0;
                    if(id==1)reframe_terminal();
                }
            }
            dragging = action == 10 ? id : 0;
            resizing = action == 11 ? id : 0;
            Rect window = *window_rect(id);
            drag_x = x - window.x; drag_y = y - window.y;
        } else if (apps_find((unsigned)id)) apps_click((unsigned)id,*window_rect(id),x,y);
        else if (id == 2 && hardware_page && y >= system_window.y + 200 && y < system_window.y + 251) {
            if (x < system_window.x + 120) {
                volume = volume == 100 ? 50 : volume == 50 ? 0 : 100;
                platform_audio_volume(volume);
                settings_volume(volume);
            } else {
                platform_audio_start(display); platform_audio_volume(volume);
            }
        } else if (id == 2 && !hardware_page) {
            int region_y = system_window.y + (system_window.height < 420 ? 219 : 345);
            if (y >= region_y && y < region_y + 48) {
                settings_zone((settings_current()->zone + 1) % 7); refresh_clock();
            }
        }
        scene_dirty = 1;
        return;
    }
    if (target == 1) { menu_open = !menu_open; scene_dirty = 1; return; }
    if (menu_open) { menu_open = 0; scene_dirty = 1; }
    if (target == 2 || target == 5 || target == 7) open_window(1);
    else if (target == 3 || target == 6 || target == 8) { hardware_page = 0; open_window(2); }
    else if (target == 9 || target == 10 || target == 11) open_window(3);
    else if (target == 12) open_window(4);
    else if (target == 15 || target == 16) open_window(APP_CALCULATOR);
    else if (target == 13 || target == 14) {
        power_dialog = target == 13 ? 2 : 1; power_error = 0;
        dragging = resizing = context_open = 0; scene_dirty = 1;
    }
    else if (target == 4) {
        active_window = 0; scene_dirty = 1;
        serial_write("DESKTOP: window closed\r\n");
    }
}

_Noreturn void desktop_run(const BootInfo *boot)
{
    if (!gui_initialize()) { console_restore_fullscreen(); shell_run(boot); }
    display = boot;
    wallpaper_style = (int)settings_current()->wallpaper;
    volume = settings_current()->volume;
    canvas = *boot;
    canvas.framebuffer = boot->desktop_buffer;
    canvas.pixels_per_scanline = boot->width;
    wallpaper = canvas;
    wallpaper.framebuffer = boot->wallpaper_buffer;
    int width = (int)boot->width, height = (int)boot->height;
    windows_initialize(&wm);
    int window_top = height < 600 ? 96 : 104;
    int maximum_height = height - window_top - 116;
    terminal_window = (Rect){0,window_top,minimum(960,width - 96),minimum(560,maximum_height)};
    terminal_window.x = (width - terminal_window.width) / 2;
    system_window = (Rect){0,window_top,minimum(690,width - 96),minimum(480,maximum_height)};
    system_window.x = (width - system_window.width) / 2;
    files_window = (Rect){0,window_top,minimum(960,width - 96),minimum(560,maximum_height)};
    files_window.x = (width - files_window.width) / 2;
    about_window = (Rect){(width - 580) / 2,window_top,580,minimum(390,maximum_height)};
    calculator_window=(Rect){(width-400)/2,window_top,400,minimum(540,maximum_height)};
    for(unsigned id=1;id<=5;++id)windows_register(&wm,id,wm.rect[id]);
    apps_initialize(&wm);
    work_area=(Rect){12,96,width-24,height-204};
    dock = (Rect){(width - 420) / 2,height - 96,420,76};
    brand_button = (Rect){dock.x + 13,dock.y + 10,50,50};
    terminal_button = (Rect){dock.x + 81,dock.y + 10,50,50};
    system_button = (Rect){dock.x + 149,dock.y + 10,50,50};
    files_button = (Rect){dock.x + 217,dock.y + 10,50,50};
    calculator_button=(Rect){dock.x+285,dock.y+10,50,50};
    about_button=(Rect){dock.x+353,dock.y+10,50,50};
    clock_panel = (Rect){width - 254,24,226,64};
    menu = (Rect){dock.x,dock.y - 355,242,340};
    power_panel = (Rect){(width - 520) / 2,(height - 240) / 2,520,240};
    platform_network_initialize();
    if (!platform_input_initialize(boot->width,boot->height)) {
        console_restore_fullscreen();
        console_write("PLATFORM INPUT INITIALIZATION FAILED.\n");
        platform_halt();
    }

    create_wallpaper();
    pointer = (PlatformPointer){width / 2,height / 2,0};
    refresh_clock(); scene(); present((Rect){0,0,width,height}); draw_pointer();
    serial_write(platform_pointer_available() ? "DESKTOP: modern UI ready / pointer enabled\r\n" : "DESKTOP: modern UI ready / keyboard shortcuts\r\n");
    unsigned ticks = 0;
    for (;;) {
        platform_network_poll();
        PlatformPointer old_pointer = pointer;
        int terminal_dirty = 0, clock_dirty = 0;
        for (unsigned count = 0; count < 64 && platform_key_pending(); ++count) {
            char key = platform_key_read();
            if (power_dialog) {
                if (key == 27) { power_dialog = 0; power_error = 0; scene_dirty = 1; }
                else if (key == '\n') confirm_power();
                continue;
            }
            if(key==DESKTOP_SWITCH_END) {windows_cycle_end(&wm);continue;}
            if(key==DESKTOP_NEXT||key==DESKTOP_PREVIOUS) {
                windows_cycle(&wm,key==DESKTOP_PREVIOUS);menu_open=context_open=dragging=resizing=0;scene_dirty=1;continue;
            }
            if(key==DESKTOP_MAXIMIZE||key==DESKTOP_LEFT||key==DESKTOP_RIGHT||key==DESKTOP_RESTORE||key==DESKTOP_MINIMIZE) {
                dragging=resizing=0;
                if(key==DESKTOP_MAXIMIZE)toggle_maximize(active_window);
                else if(key==DESKTOP_MINIMIZE){hide_window(active_window);scene_dirty=1;}
                else arrange_window(active_window,key==DESKTOP_LEFT?WINDOW_LEFT:key==DESKTOP_RIGHT?WINDOW_RIGHT:WINDOW_NORMAL);
                continue;
            }
            if (key == 17) { menu_open = !menu_open; context_open = 0; scene_dirty = 1; continue; }
            if (menu_open && (key == 'r' || key == 'R' || key == 's' || key == 'S')) {
                power_dialog = (key == 'r' || key == 'R') ? 2 : 1;
                power_error = 0; menu_open = dragging = resizing = 0; scene_dirty = 1; continue;
            }
            if (key == 1) open_window(1);
            else if (key == 2) { hardware_page = 0; open_window(2); }
            else if (key == 3) { hardware_page = 1; open_window(2); }
            else if (key == 4) open_window(3);
            else if (key == 5) open_window(4);
            else if (key == 20) open_window(APP_CALCULATOR);
            else if (key == 6 && active_window == APP_NOTES) { apps_key(APP_NOTES,key); scene_dirty = 1; }
            else if (key == 27) {
                if(apps_escape((unsigned)active_window)){scene_dirty=1;continue;}
                if (context_open || menu_open) context_open = menu_open = 0;
                else if (active_window) {
                    if(apps_find((unsigned)active_window))apps_close((unsigned)active_window);else windows_close(&wm,(unsigned)active_window);
                }
                scene_dirty = 1;
            }
            else if (key && active_window == 1) { shell_key(key); terminal_dirty = 1; }
            else if (key && apps_find((unsigned)active_window)) { apps_key((unsigned)active_window,key); scene_dirty = 1; }
        }
        PlatformPointer event;
        for (unsigned count = 0; count < 64 && platform_pointer_read(&event); ++count) {
            if (!power_dialog && (event.buttons & 2) && !(pointer.buttons & 2) && target_at(event.x,event.y) < 100) {
                context_menu = (Rect){minimum(event.x,width - 258),minimum(event.y,height - 158),250,156};
                context_open = 1; menu_open = 0; scene_dirty = 1;
                serial_write("DESKTOP: context menu opened\r\n");
            }
            if ((event.buttons & 1) && !(pointer.buttons & 1)) click(event.x,event.y);
            if (!(event.buttons & 1)) {
                if(dragging && (pointer.buttons&1)) {
                    if(event.y<=12)arrange_window(dragging,WINDOW_MAXIMIZED);
                    else if(event.x<=12)arrange_window(dragging,WINDOW_LEFT);
                    else if(event.x>=width-13)arrange_window(dragging,WINDOW_RIGHT);
                }
                dragging = resizing = 0;
            }
            if ((dragging || resizing) && (event.buttons & 1)) {
                int id = dragging ? dragging : resizing;
                Rect *window = window_rect(id);
                if (dragging) {
                    window->x = event.x - drag_x; window->y = event.y - drag_y;
                    if (window->x < 0) window->x = 0;
                    if (window->y < 0) window->y = 0;
                    window->x = minimum(window->x,width - window->width);
                    window->y = minimum(window->y,height - 100 - window->height);
                } else {
                    const AppDefinition *app=apps_find((unsigned)id);
                    int min_width = app?app->min_width:id == 1 ? 420 : 544;
                    int min_height = app?app->min_height:id == 1 ? 220 : 260;
                    window->width = event.x - window->x + 1;
                    window->height = event.y - window->y + 1;
                    if (window->width < min_width) window->width = min_width;
                    if (window->height < min_height) window->height = min_height;
                    window->width = minimum(window->width,width - window->x);
                    window->height = minimum(window->height,height - 100 - window->y);
                }
                if (id == 1) console_reframe(window->x + 16,window->y + 64,window->width - 32,window->height - 80);
                scene_dirty = 1;
            }
            if(apps_find((unsigned)active_window)&&!power_dialog&&!dragging&&!resizing){
                apps_pointer((unsigned)active_window,*window_rect(active_window),event.x,event.y,event.buttons);
                if(event.buttons&1)scene_dirty=1;
            }
            pointer = event;
        }
        int target = target_at(pointer.x,pointer.y);
        if (target != hovered) { hovered = target; scene_dirty = 1; }
        if (++ticks == 40) { ticks = 0; refresh_clock(); clock_dirty = 1; }
        int moved = pointer.x != old_pointer.x || pointer.y != old_pointer.y;
        if (scene_dirty || terminal_dirty || clock_dirty || moved) {
            present((Rect){old_pointer.x,old_pointer.y,16,24});
            if (scene_dirty || terminal_dirty || clock_dirty) {
                scene(); present((Rect){0,0,width,height}); scene_dirty = 0;
            } else {
                if (terminal_dirty) present(terminal_window);
                if (clock_dirty) { restore_wallpaper(clock_panel); clock_widget(0); present(clock_panel); }
            }
            draw_pointer();
        }
        screen_wait_frame();
    }
}
