#ifndef YAAI_EDITOR_H
#define YAAI_EDITOR_H
#include "edit_keys.h"
#include "files.h"
enum { EDIT_HISTORY = 17 };
typedef struct {
    char text[FILE_BYTES + 1];
    unsigned length, cursor, anchor;
} EditSnapshot;
typedef struct {
    char text[FILE_BYTES + 1];
    unsigned length, cursor, anchor, scroll, columns, rows;
    EditSnapshot history[EDIT_HISTORY];
    unsigned current, count;
} Editor;
void editor_reset(Editor *e, const char *text, unsigned length);
void editor_layout(Editor *e, unsigned columns, unsigned rows);
int editor_key(Editor *e, unsigned key);
void editor_point(Editor *e, unsigned row, unsigned column, int extend);
unsigned editor_line(const Editor *e, unsigned position);
unsigned editor_column(const Editor *e, unsigned position);
#endif
