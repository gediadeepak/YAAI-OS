#include "window_manager.h"
void windows_initialize(WindowManager *m) { *m = (WindowManager){0}; }
static int registered(WindowManager *m, unsigned id) {
    if (!m || !id || id >= WINDOW_LIMIT)
        return 0;
    for (unsigned i = 0; i < m->count; ++i)
        if (m->order[i] == (int)id)
            return 1;
    return 0;
}
int windows_register(WindowManager *m, unsigned id, GuiRect rect) {
    if (!m || !id || id >= WINDOW_LIMIT || m->count == WINDOW_LIMIT - 1 || rect.width < 1 ||
        rect.height < 1 || registered(m, id))
        return 0;
    m->rect[id] = m->restore[id] = rect;
    m->order[m->count++] = (int)id;
    return 1;
}
void windows_cycle_end(WindowManager *m) {
    if (m)
        m->cycle_count = 0;
}
int windows_focus(WindowManager *m, unsigned id) {
    if (!registered(m, id))
        return 0;
    windows_cycle_end(m);
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
    m->opened[id] = m->visible[id] = 1;
    return 1;
}
void windows_hide(WindowManager *m, unsigned id) {
    if (!registered(m, id))
        return;
    windows_cycle_end(m);
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
void windows_close(WindowManager *m, unsigned id) {
    if (!registered(m, id))
        return;
    windows_hide(m, id);
    m->opened[id] = 0;
    if (m->layout[id] != WINDOW_NORMAL)
        m->rect[id] = m->restore[id];
    m->layout[id] = WINDOW_NORMAL;
}
int windows_cycle(WindowManager *m, int reverse) {
    if (!m)
        return 0;
    if (!m->cycle_count) {
        m->cycle_index = 0;
        for (unsigned i = m->count; i; --i)
            if (m->opened[m->order[i - 1]]) {
                if (m->order[i - 1] == m->active)
                    m->cycle_index = m->cycle_count;
                m->cycle[m->cycle_count++] = m->order[i - 1];
            }
    }
    unsigned count = m->cycle_count;
    if (!count)
        return 0;
    unsigned index = m->active ? (m->cycle_index + (reverse ? count - 1 : 1)) % count
                     : reverse ? count - 1
                               : 0;
    int id = m->cycle[index];
    windows_open(m, (unsigned)id);
    m->cycle_count = count;
    m->cycle_index = index;
    return id;
}
int windows_arrange(WindowManager *m, unsigned id, int layout, GuiRect area, int min_width, int min_height) {
    if (!registered(m, id) || !m->visible[id] || layout < WINDOW_NORMAL || layout > WINDOW_RIGHT)
        return 0;
    if (layout == WINDOW_NORMAL) {
        if (m->layout[id] != WINDOW_NORMAL)
            m->rect[id] = m->restore[id];
        m->layout[id] = WINDOW_NORMAL;
        return 1;
    }
    if (area.width < 1 || area.height < 1)
        return 0;
    GuiRect next = area;
    if (layout == WINDOW_LEFT || layout == WINDOW_RIGHT) {
        int half = (area.width - 8) / 2;
        next.width = half;
        if (layout == WINDOW_RIGHT) {
            next.x += half + 8;
            next.width = area.width - half - 8;
        }
    }
    if (next.width < min_width || next.height < min_height)
        return 0;
    if (m->layout[id] == WINDOW_NORMAL)
        m->restore[id] = m->rect[id];
    m->rect[id] = next;
    m->layout[id] = layout;
    return 1;
}
