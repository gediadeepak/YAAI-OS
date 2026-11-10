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
#include "settings_app.h"
#include "shell_ui.h"
#include "quick_settings.h"




typedef GuiRect Rect;
static const BootInfo *display;
static BootInfo canvas, wallpaper;
static WindowManager wm;
#define terminal_window wm.rect[1]
#define system_window wm.rect[2]
#define notes_window wm.rect[3]
#define files_window wm.rect[6]
#define about_window wm.rect[4]
#define calculator_window wm.rect[5]
#define active_window wm.active
#define visible wm.visible
#define order wm.order
static Rect dock, brand_button, terminal_button, system_button, files_button, calculator_button, about_button, notes_button, clock_panel, menu;
static PlatformPointer pointer;
static ShellLauncher launcher;
static uint64_t hover_since;
static QuickSettings quick;
static Rect quick_panel;
static int quick_open;
static void close_quick(void){quick_settings_cancel(&quick);quick_open=0;}
static int menu_open, terminal_started, scene_dirty, hovered;
static int dragging, resizing, drag_x, drag_y, wallpaper_style;
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

static void brand_symbol(int x,int y) {shell_icon(&canvas,0,(Rect){x,y,36,36});}
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
    ShellPalette p=shell_palette(&canvas);
    if(shadow)gui_shadow(&canvas,clock_panel,18);
    gui_round_rect(&canvas,clock_panel,18,p.background,245);
    int x=clock_panel.x,y=clock_panel.y;
    gui_round_rect(&canvas,(Rect){x+20,y+20,22,22},11,p.muted,255);
    gui_round_rect(&canvas,(Rect){x+22,y+22,18,18},9,p.surface,255);
    gui_line(&canvas,x+31,y+24,x+31,y+31,1,p.accent);
    gui_line(&canvas,x+31,y+31,x+36,y+33,1,p.accent);
    label(x+58,y+7,time_text,GUI_BODY,p.text);
    label(x+58,y+34,date_text,GUI_SMALL,p.muted);
}
static void draw_dock(void)
{
    ShellPalette p=shell_palette(&canvas);
    gui_shadow(&canvas,dock,24);
    gui_round_rect(&canvas,dock,24,p.border,255);
    gui_round_rect(&canvas,(Rect){dock.x+1,dock.y+1,dock.width-2,dock.height-2},23,p.background,250);
    Rect buttons[7]={brand_button,files_button,notes_button,system_button,terminal_button,calculator_button,about_button};
    const int targets[7]={1,9,17,3,2,15,12}, ids[7]={0,6,3,2,1,5,4};
    const char *names[7]={"Apps (F10)","Files","Notes","Settings","Terminal","Calculator","About YAAI OS"};
    gui_line(&canvas,dock.x+74,dock.y+20,dock.x+74,dock.y+58,1,p.border);
    for(int i=0;i<7;++i) {
        int selected=i?active_window==ids[i]:menu_open;
        if(hovered==targets[i]||selected)gui_round_rect(&canvas,buttons[i],15,p.hover,255);
        shell_icon(&canvas,(unsigned)ids[i],(Rect){buttons[i].x+6,buttons[i].y+6,44,44});
        if(i&&wm.opened[ids[i]]) {
            int w=selected?18:4;
            gui_round_rect(&canvas,(Rect){buttons[i].x+(56-w)/2,dock.y+73,w,3},1,selected?p.accent:p.muted,255);
        }
        if(!quick_open&&!menu_open&&!context_open&&!power_dialog&&hovered==targets[i]&&apps_ticks()-hover_since>=12)
            shell_tooltip(&canvas,buttons[i],names[i]);
    }
}
static void chrome(int id, Rect window, const char *title)
{
    int maximized=wm.layout[id]==WINDOW_MAXIMIZED;
    int settings_style=(id==APP_SETTINGS||id==APP_FILES)&&!settings_dark();
    uint32_t controls=settings_style?rgb(80,96,114):rgb(170,196,215);
    if(!maximized)gui_shadow(&canvas,window,20);
    gui_round_rect(&canvas,window,maximized?0:20,settings_style?rgb(204,214,226):rgb(61,81,103),220);
    gui_round_rect(&canvas,(Rect){window.x + 1,window.y + 1,window.width - 2,window.height - 2},maximized?0:19,settings_style?rgb(244,246,250):rgb(14,23,39),250);
    label(window.x + 25,window.y + 15,title,GUI_BODY,settings_style?rgb(31,43,61):rgb(218,232,247));
    gui_line(&canvas,window.x + 20,window.y + 51,window.x + window.width - 21,window.y + 51,1,settings_style?rgb(224,230,238):rgb(36,52,73));
    int x = window.x + window.width - 35, y = window.y + 25;
    gui_round_rect(&canvas,(Rect){x - 12,y - 12,24,24},12,hovered == 100*id+4 ? rgb(143,69,78) : settings_style?rgb(224,229,237):rgb(57,47,64),255);
    gui_line(&canvas,x - 4,y - 4,x + 4,y + 4,1,settings_style?rgb(89,102,121):rgb(236,190,192));
    gui_line(&canvas,x + 4,y - 4,x - 4,y + 4,1,settings_style?rgb(89,102,121):rgb(236,190,192));
    gui_line(&canvas,x - 82,y + 3,x - 70,y + 3,2,controls);
    int bx=x-40;
    if(hovered==100*id+13)gui_round_rect(&canvas,(Rect){bx-15,y-15,30,30},8,rgb(46,72,92),255);
    if(wm.layout[id]==WINDOW_MAXIMIZED) {
        gui_line(&canvas,bx-3,y-6,bx+6,y-6,1,controls);
        gui_line(&canvas,bx+6,y-6,bx+6,y+3,1,controls);
    }
    gui_line(&canvas,bx-6,y-3,bx+3,y-3,1,controls);
    gui_line(&canvas,bx-6,y-3,bx-6,y+6,1,controls);
    gui_line(&canvas,bx+3,y-3,bx+3,y+6,1,controls);
    gui_line(&canvas,bx-6,y+6,bx+3,y+6,1,controls);
    gui_line(&canvas,window.x + window.width - 22,window.y + window.height - 9,
             window.x + window.width - 9,window.y + window.height - 22,2,rgb(106,146,164));
}

static void system_panel(void) {chrome(2,system_window,"Settings");apps_draw(2,&canvas,system_window);}

static void scene(void)
{
    ShellPalette palette=shell_palette(&canvas);
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
                centered(about_window,y + (compact ? 232 : 322),"F1 Terminal   F2 Settings   F3 Devices   F4 Files",GUI_SMALL,rgb(151,173,194));
            }
        }
    }
    if(!fullscreen_window()) {clock_widget(1);draw_dock();}
    if(menu_open)shell_launcher_draw(&canvas,&launcher,menu);
    if (context_open) {
        gui_shadow(&canvas,context_menu,14);
        gui_round_rect(&canvas,context_menu,14,palette.background,255);
        const char *items[] = {"AI human wallpaper", "Midnight wallpaper", "Teal wallpaper", "Devices and sound"};
        for (int index = 0; index < 4; ++index) {
            if (hovered == 20 + index)
                gui_round_rect(&canvas,(Rect){context_menu.x + 6,context_menu.y + 6 + index * 36,238,34},8,palette.hover,255);
            label(context_menu.x + 16,context_menu.y + 15 + index * 36,items[index],GUI_SMALL,palette.text);
        }
    }
    if(quick_open){QuickInfo info=quick_settings_info();quick_settings_draw(&canvas,&quick,&info,quick_panel);}
    if (power_dialog) {
        Rect p = power_panel;
        gui_round_rect(&canvas,(Rect){0,0,(int)canvas.width,(int)canvas.height},0,rgb(3,8,16),150);
        gui_shadow(&canvas,p,20); gui_round_rect(&canvas,p,20,palette.background,255);
        label(p.x + 26,p.y + 24,power_dialog == 2 ? "Restart YAAI OS?" : "Shut down YAAI OS?",GUI_HEADING,palette.text);
        label(p.x + 26,p.y + 81,file_app_has_changes() ? "Your open note will be saved first." : "Saved files will be flushed to disk.",GUI_SMALL,palette.muted);
        label(p.x + 26,p.y + 113,power_error ? power_error : "Cancel to keep working. Enter to continue.",GUI_SMALL,power_error ? rgb(199,75,69) : palette.muted);
        Rect cancel = {p.x + 24,p.y + 168,148,40}, confirm = {p.x + p.width - 196,p.y + 168,172,40};
        gui_round_rect(&canvas,cancel,10,palette.surface,255);
        gui_round_rect(&canvas,confirm,10,palette.hover,255);
        centered(cancel,cancel.y + 11,"Cancel",GUI_SMALL,palette.text);
        centered(confirm,confirm.y + 11,power_dialog == 2 ? "Restart" : "Shut down",GUI_SMALL,palette.text);
    }
}
static void confirm_power(void)
{
    if (!file_app_save_pending()) power_error = "Save failed. Cancel and check Notes.";
    else power_error = power_finish(display,power_dialog == 2);
    scene_dirty = 1;
}

static void open_window(int window)
{
    close_quick();
    if(apps_find((unsigned)window))apps_launch((unsigned)window);
    else windows_open(&wm,(unsigned)window);
    const AppDefinition *opened=apps_find((unsigned)window);
    if(opened&&(wm.rect[window].width<opened->min_width||wm.rect[window].height<opened->min_height))arrange_window(window,WINDOW_MAXIMIZED);
    menu_open = 0;
    context_open = 0;
    if (window == 1 && !terminal_started) {
        console_set_viewport(&canvas,terminal_window.x + 16,terminal_window.y + 64,
                             terminal_window.width - 32,terminal_window.height - 80);
        shell_begin(display);
        terminal_started = 1;
    }
    scene_dirty = 1;
    serial_write(window == 1 ? "DESKTOP: Terminal opened\r\n" : window == 2 ? "DESKTOP: Settings opened\r\n" : window == APP_FILES ? "DESKTOP: Files opened\r\n" : window == APP_NOTES ? "DESKTOP: Notes opened\r\n" : window == APP_CALCULATOR ? "DESKTOP: Calculator opened\r\n" : "DESKTOP: About opened\r\n");
}

static int target_at(int x, int y)
{
    if (power_dialog) {
        if (inside((Rect){power_panel.x + 24,power_panel.y + 168,148,40},x,y)) return 31;
        if (inside((Rect){power_panel.x + power_panel.width - 196,power_panel.y + 168,172,40},x,y)) return 32;
        return 30;
    }
    if(quick_open&&inside(quick_panel,x,y))return 48;
    if (context_open && inside(context_menu,x,y)) {
        int item = (y - context_menu.y - 6) / 36;
        return item >= 0 && item < 4 ? 20 + item : 0;
    }
    if(menu_open&&inside(menu,x,y))return shell_launcher_hit(&launcher,menu,x,y);
    if(!fullscreen_window()) {
    if (inside(clock_panel,x,y)) return 49;
    if (inside(brand_button,x,y)) return 1;
    if (inside(terminal_button,x,y)) return 2;
    if (inside(system_button,x,y)) return 3;
    if (inside(files_button,x,y)) return 9;
    if (inside(calculator_button,x,y)) return 15;
    if (inside(about_button,x,y)) return 12;
    if (inside(notes_button,x,y)) return 17;
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

static void activate_shell_target(int target)
{
    close_quick();menu_open=context_open=0;scene_dirty=1;
    if(target==2)open_window(1);
    else if(target==3){settings_app_select(6);open_window(2);}
    else if(target==SHELL_DEVICES){settings_app_select(5);open_window(2);}
    else if(target==9)open_window(APP_FILES);
    else if(target==17)open_window(APP_NOTES);
    else if(target==12)open_window(4);
    else if(target==15)open_window(APP_CALCULATOR);
    else if(target==13||target==14){power_dialog=target==13?2:1;power_error=0;dragging=resizing=0;}
}
static void toggle_launcher(void)
{
    close_quick();menu_open=!menu_open;
    if(menu_open)shell_launcher_init(&launcher);
    context_open=dragging=resizing=0;scene_dirty=1;
}
static void toggle_quick(void)
{
    if(quick_open)close_quick();
    else {QuickInfo info=quick_settings_info();quick_settings_open(&quick,&info);quick_open=1;}
    menu_open=context_open=dragging=resizing=0;scene_dirty=1;
}
static void quick_action(int action)
{
    if(action==QUICK_CLOSE)close_quick();
    else if(action)activate_shell_target(action);
    scene_dirty=1;
}
static void click(int x, int y)
{
    int target = target_at(x,y);
    if (power_dialog) {
        if (target == 31) { power_dialog = 0; power_error = 0; scene_dirty = 1; }
        else if (target == 32) confirm_power();
        return;
    }
    if(target==49){toggle_quick();return;}
    if(quick_open){
        if(inside(quick_panel,x,y)){QuickInfo info=quick_settings_info();quick_action(quick_settings_click(&quick,quick_panel,&info,x,y));}
        else {close_quick();scene_dirty=1;}
        return;
    }
    if(menu_open&&inside(menu,x,y)) {
        if(target==SHELL_CLEAR){shell_launcher_init(&launcher);scene_dirty=1;}
        else if(target&&target!=SHELL_SEARCH)activate_shell_target(target);
        return;
    }
    if (target >= 20 && target <= 23) {
        context_open = 0;
        if (target == 23) { settings_app_select(5); open_window(2); }
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
        scene_dirty = 1;
        return;
    }
    if(target==1){toggle_launcher();return;}
    activate_shell_target(target);
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
    int settings_top=height<720?24:window_top;
    system_window = (Rect){0,settings_top,minimum(1160,width - 48),minimum(800,height-settings_top-108)};
    system_window.x = (width - system_window.width) / 2;
    notes_window = (Rect){0,window_top,minimum(860,width - 96),minimum(520,maximum_height)};
    notes_window.x=(width-notes_window.width)/2;
    files_window = (Rect){0,window_top,minimum(1000,width - 64),minimum(600,maximum_height)};
    files_window.x = (width - files_window.width) / 2;
    about_window = (Rect){(width - 580) / 2,window_top,580,minimum(390,maximum_height)};
    calculator_window=(Rect){(width-400)/2,window_top,400,minimum(540,maximum_height)};
    for(unsigned id=1;id<=6;++id)windows_register(&wm,id,wm.rect[id]);
    apps_initialize(&wm);
    work_area=(Rect){12,96,width-24,height-204};
    dock=(Rect){(width-472)/2,height-104,472,84};
    brand_button=(Rect){dock.x+12,dock.y+10,56,56};
    files_button=(Rect){dock.x+80,dock.y+10,56,56};
    notes_button=(Rect){dock.x+144,dock.y+10,56,56};
    system_button=(Rect){dock.x+208,dock.y+10,56,56};
    terminal_button=(Rect){dock.x+272,dock.y+10,56,56};
    calculator_button=(Rect){dock.x+336,dock.y+10,56,56};
    about_button=(Rect){dock.x+400,dock.y+10,56,56};
    clock_panel=(Rect){width-254,24,226,64};
    quick_panel=quick_settings_bounds((unsigned)width,(unsigned)height);
    menu=shell_launcher_bounds((unsigned)width,(unsigned)height,dock);
    shell_launcher_init(&launcher);
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
    unsigned last_theme=settings_current()->theme,last_zone=settings_current()->zone,last_color=settings_current()->color_mode;
    for (;;) {
        if(wallpaper_style!=(int)settings_current()->wallpaper){wallpaper_style=(int)settings_current()->wallpaper;create_wallpaper();scene_dirty=1;}
        if(volume!=settings_current()->volume){volume=settings_current()->volume;platform_audio_volume(volume);scene_dirty=1;}
        if(last_theme!=settings_current()->theme){last_theme=settings_current()->theme;scene_dirty=1;}
        if(last_zone!=settings_current()->zone){last_zone=settings_current()->zone;refresh_clock();scene_dirty=1;}
        if(last_color!=settings_current()->color_mode){last_color=settings_current()->color_mode;scene_dirty=1;}
        apps_tick();
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
            if(key==DESKTOP_QUICK){toggle_quick();continue;}
            if(key==DESKTOP_SWITCH_END) {windows_cycle_end(&wm);continue;}
            if(key==DESKTOP_NEXT||key==DESKTOP_PREVIOUS) {
                close_quick();windows_cycle(&wm,key==DESKTOP_PREVIOUS);menu_open=context_open=dragging=resizing=0;scene_dirty=1;continue;
            }
            if(key==DESKTOP_MAXIMIZE||key==DESKTOP_LEFT||key==DESKTOP_RIGHT||key==DESKTOP_RESTORE||key==DESKTOP_MINIMIZE) {
                close_quick();dragging=resizing=0;
                if(key==DESKTOP_MAXIMIZE)toggle_maximize(active_window);
                else if(key==DESKTOP_MINIMIZE){hide_window(active_window);scene_dirty=1;}
                else arrange_window(active_window,key==DESKTOP_LEFT?WINDOW_LEFT:key==DESKTOP_RIGHT?WINDOW_RIGHT:WINDOW_NORMAL);
                continue;
            }
            if(key==17){toggle_launcher();continue;}
            if(quick_open){QuickInfo info=quick_settings_info();quick_action(quick_settings_key(&quick,&info,key));continue;}
            if(menu_open){
                int action=shell_launcher_key(&launcher,key);
                if(action==SHELL_CLOSE)menu_open=0;
                else if(action)activate_shell_target(action);
                scene_dirty=1;continue;
            }
            if (key == 1) open_window(1);
            else if (key == 2) { settings_app_select(6); open_window(2); }
            else if (key == 3) { settings_app_select(5); open_window(2); }
            else if (key == 4) open_window(APP_FILES);
            else if (key == 30) open_window(APP_NOTES);
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
            int popup_pointer=quick_open;
            if (!quick_open && !menu_open && !power_dialog && (event.buttons & 2) && !(pointer.buttons & 2) && target_at(event.x,event.y) < 100) {
                context_menu = (Rect){minimum(event.x,width - 258),minimum(event.y,height - 158),250,156};
                context_open = 1; menu_open = 0; scene_dirty = 1;
                serial_write("DESKTOP: context menu opened\r\n");
            }
            if(!quick_open&&!power_dialog&&(event.buttons&2)&&!(pointer.buttons&2)){int target=target_at(event.x,event.y);if(target>=100)focus(target/100);}
            if ((event.buttons & 1) && !(pointer.buttons & 1)) click(event.x,event.y);
            if(quick_open){QuickInfo info=quick_settings_info();if(quick_settings_pointer(&quick,quick_panel,&info,event.x,event.y,event.buttons))scene_dirty=1;}
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
            if(apps_find((unsigned)active_window)&&!popup_pointer&&!quick_open&&!menu_open&&!power_dialog&&!dragging&&!resizing){
                apps_pointer((unsigned)active_window,*window_rect(active_window),event.x,event.y,event.buttons);
                if(event.buttons&3)scene_dirty=1;
            }
            if(menu_open&&(pointer.x!=event.x||pointer.y!=event.y)&&shell_launcher_hover(&launcher,menu,event.x,event.y))scene_dirty=1;
            pointer = event;
        }
        int target = target_at(pointer.x,pointer.y);
        if(target!=hovered){hovered=target;hover_since=apps_ticks();scene_dirty=1;}
        if(apps_ticks()-hover_since==12)scene_dirty=1;
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
