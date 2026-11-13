#ifndef YAAI_FILE_APP_H
#define YAAI_FILE_APP_H
#include "gui.h"
void file_app_open(void);
int file_app_tick(void);
int file_app_recovery_pending(void);
void file_app_draw(const BootInfo *canvas, GuiRect window);
void file_app_click(GuiRect window, int x, int y);
void file_app_pointer(GuiRect window, int x, int y, unsigned buttons);
void file_app_key(char key);
int file_app_has_changes(void);
int file_app_save_pending(void);
int file_app_escape(void);
int file_app_close(void);
int file_app_load(unsigned id);
int file_app_new(unsigned parent);
int file_app_in_use(unsigned id);
unsigned file_app_document_id(void);
#endif
