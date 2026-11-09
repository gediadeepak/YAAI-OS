#include "window_manager.h"
void windows_initialize(WindowManager *m) { *m = (WindowManager){0}; }
int windows_register(WindowManager *m, unsigned id, GuiRect rect) {
    if (!m || !id || id >= WINDOW_LIMIT || m->count == WINDOW_LIMIT - 1 || rect.width < 1 || rect.height < 1)
        return 0;
    for (unsigned i = 0; i < m->count; ++i)
        if (m->order[i] == (int)id)
            return 0;
    m->rect[id] = rect;
    m->order[m->count++] = (int)id;
    return 1;
}
int windows_focus(WindowManager *m, unsigned id) {
    if (!m || !id || id >= WINDOW_LIMIT)
        return 0;
    for (unsigned i = 0; i < m->count; ++i)
        if (m->order[i] == (int)id) {
            for (unsigned j = i; j + 1 < m->count; ++j)
                m->order[j] = m->order[j + 1];
            m->order[m->count - 1] = (int)id;
            m->active = (int)id;
            return 1;
        }
    return 0;
}
int windows_open(WindowManager *m, unsigned id) {
    if (!windows_focus(m, id))
        return 0;
    m->visible[id] = 1;
    return 1;
}
void windows_hide(WindowManager *m, unsigned id) {
    if (!m || !id || id >= WINDOW_LIMIT)
        return;
    m->visible[id] = 0;
    if (m->active != (int)id)
        return;
    m->active = 0;
    for (unsigned i = m->count; i; --i)
        if (m->visible[m->order[i - 1]]) {
            m->active = m->order[i - 1];
            break;
        }
}
