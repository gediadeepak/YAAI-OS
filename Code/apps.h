#ifndef YAAI_APPS_H
#define YAAI_APPS_H
#include "files.h"
#include "window_manager.h"
enum { APP_NOTES = 3, APP_CALCULATOR = 5 };
typedef struct {
    FileResult (*read)(const char *, void *, unsigned *);
    FileResult (*write)(const char *, const void *, unsigned);
    FileResult (*read_id)(unsigned, void *, unsigned *);
    FileResult (*write_at)(unsigned, const char *, const void *, unsigned);
    FileResult (*list)(unsigned, int, FileEntry *, unsigned, unsigned *);
    FileResult (*entry)(unsigned, FileEntry *);
    FileResult (*resolve)(const char *, unsigned *);
    FileResult (*path)(unsigned, char *, unsigned);
    FileResult (*usage)(FileUsage *);
    FileResult (*mkdir)(unsigned, const char *);
    FileResult (*rename)(unsigned, const char *);
    FileResult (*move)(unsigned, unsigned);
    FileResult (*trash)(unsigned);
    FileResult (*restore)(unsigned);
    FileResult (*remove)(unsigned);
} AppFiles;
typedef struct {
    const AppFiles *files;
} AppServices;
typedef struct {
    unsigned id;
    const char *title;
    int min_width, min_height;
    void (*open)(const AppServices *);
    void (*draw)(const BootInfo *, GuiRect);
    void (*key)(char);
    void (*click)(GuiRect, int, int);
    void (*pointer)(GuiRect, int, int, unsigned);
    int (*escape)(void);
    int (*close)(void);
} AppDefinition;
void apps_initialize(WindowManager *manager);
const AppDefinition *apps_find(unsigned id);
int apps_launch(unsigned id);
int apps_close(unsigned id);
void apps_draw(unsigned id, const BootInfo *screen, GuiRect window);
void apps_key(unsigned id, char key);
void apps_click(unsigned id, GuiRect window, int x, int y);
void apps_pointer(unsigned id, GuiRect window, int x, int y, unsigned buttons);
int apps_escape(unsigned id);
extern const AppDefinition notes_application, calculator_application;
#endif
