#ifndef YAAI_FILE_APP_H
#define YAAI_FILE_APP_H
#include "gui.h"
void file_app_open(void);
void file_app_draw(const BootInfo *canvas, GuiRect window);
void file_app_click(GuiRect window, int x, int y);
void file_app_pointer(GuiRect window,int x,int y,unsigned buttons);
void file_app_key(char key);
int file_app_has_changes(void);
int file_app_save_pending(void);
int file_app_escape(void);
#endif
