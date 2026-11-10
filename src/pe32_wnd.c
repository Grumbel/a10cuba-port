/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_wnd.h"
#include "pe32_user.h"
#include "pe32_winmm.h"
#include "pe32_proc.h"

/* On non-i386 hosts the resolver table is compiled out, leaving the
 * implementations unreferenced; they are still built to catch errors. */
#pragma GCC diagnostic ignored "-Wunused-function"

enum {
    MAX_WINDOWS = 256, MAX_CLASSES = 64, MAX_QUEUE = 512, MAX_TIMERS = 32,
    MAX_PROPS = 8, HWND_BASE = 0x00020000, TEXT_MAX = 128,
    WM_NULL = 0, WM_CREATE = 1, WM_DESTROY = 2, WM_SIZE = 5, WM_SETTEXT = 12,
    WM_GETTEXT = 13, WM_GETTEXTLENGTH = 14, WM_CLOSE = 16, WM_QUIT = 18,
    WM_ERASEBKGND = 20, WM_SHOWWINDOW = 24, WM_NCCREATE = 0x81,
    WM_NCDESTROY = 0x82, WM_COMMAND = 0x111, WM_TIMER = 0x113,
    WM_INITDIALOG = 0x110, WM_SETFONT = 0x30,
    WS_CHILD = 0x40000000, WS_VISIBLE = 0x10000000, WS_DISABLED = 0x08000000,
    GWL_WNDPROC = -4, GWL_HINSTANCE = -6, GWL_HWNDPARENT = -8, GWL_STYLE = -16,
    GWL_EXSTYLE = -20, GWL_USERDATA = -21, GWL_ID = -12,
    PM_REMOVE = 1, HCBT_CREATEWND = 3
};

typedef pe32_u32 (PE32_WINAPI *wndproc_fn)(pe32_u32, pe32_u32, pe32_u32,
                                           pe32_u32);
typedef pe32_u32 (PE32_WINAPI *hookproc_fn)(pe32_u32, pe32_u32, pe32_u32);

struct wnd_class {
    char name[48];
    pe32_u32 style, proc, extra, wnd_extra, instance, icon, cursor, brush;
    int used;
};

struct wnd {
    int used;
    struct wnd_class *cls;
    pe32_u32 proc, style, exstyle, parent, id, instance, user_data, owner;
    int rect[4];
    char text[TEXT_MAX];
    pe32_u32 extra[8];
    struct { char name[24]; pe32_u32 value; } props[MAX_PROPS];
    int visible, enabled, ended;
};

struct msg {
    pe32_u32 hwnd, message, wparam, lparam, time, x, y;
};

struct timer {
    pe32_u32 hwnd, id, interval, proc, next;
    int used;
};

static struct wnd_class classes[MAX_CLASSES];
static struct wnd windows[MAX_WINDOWS];
static struct msg queue[MAX_QUEUE];
static struct timer timers[MAX_TIMERS];
static pe32_u32 queue_head, queue_tail;
#define LB_GETCURSEL_MSG 0x188u
static int mission_choice = -1, mission_pick = -1;
static pe32_u32 game_window, focus, active, capture, quit_pending, quit_code;
static pe32_u32 modal_dialog;
static int auto_accept;
static pe32_wnd_idle_fn idle_hook;

void pe32_wnd_set_idle(pe32_wnd_idle_fn idle) { idle_hook = idle; }
void pe32_wnd_set_auto_accept(int enable) { auto_accept = enable; }
static pe32_wnd_ui_fn ui_hook;
void pe32_wnd_set_ui(pe32_wnd_ui_fn ui) { ui_hook = ui; }

static struct wnd *lookup(pe32_u32 hwnd)
{
    pe32_u32 i = hwnd - HWND_BASE;
    return i < MAX_WINDOWS && windows[i].used ? &windows[i] : 0;
}

static pe32_u32 now_ms(void) { return pe32_win32_time_get_time(); }

static int str_eq_ci(const char *a, const char *b)
{
    while (*a && ((*a | 32) == (*b | 32))) {
        a++;
        b++;
    }
    return !*a && !*b;
}

static void str_copy(char *to, const char *from, pe32_u32 max)
{
    pe32_u32 i = 0;
    if (from) {
        for (; i + 1 < max && from[i]; i++) {
            to[i] = from[i];
        }
    }
    to[i] = 0;
}

static pe32_u32 str_len(const char *s)
{
    pe32_u32 n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* ---- classes ---------------------------------------------------------- */

static pe32_u32 PE32_WINAPI def_window_proc(pe32_u32 hwnd, pe32_u32 msg,
                                            pe32_u32 wparam, pe32_u32 lparam);
static pe32_u32 PE32_WINAPI user_destroy_window(pe32_u32 hwnd);

static struct wnd_class *find_class(const char *name)
{
    int i;
    if ((unsigned long)name <= 0xffffu) {
        return 0; /* atoms unsupported */
    }
    for (i = 0; i < MAX_CLASSES; i++) {
        if (classes[i].used && str_eq_ci(classes[i].name, name)) {
            return &classes[i];
        }
    }
    return 0;
}

static struct wnd_class *add_class(const char *name, pe32_u32 proc)
{
    int i;
    for (i = 0; i < MAX_CLASSES; i++) {
        if (!classes[i].used) {
            classes[i].used = 1;
            str_copy(classes[i].name, name, sizeof classes[i].name);
            classes[i].proc = proc;
            return &classes[i];
        }
    }
    return 0;
}

static void ensure_builtin_classes(void)
{
    static const char *const names[] = {"BUTTON", "STATIC", "EDIT", "COMBOBOX",
                                        "LISTBOX", "SCROLLBAR", "#32770"};
    unsigned i;
    if (classes[0].used) {
        return;
    }
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        add_class(names[i], (pe32_u32)(unsigned long)&def_window_proc);
    }
}

/* WNDCLASSA: style, proc, clsExtra, wndExtra, instance, icon, cursor, brush,
 * menu name, class name. */
static pe32_u32 PE32_WINAPI user_register_class_a(const pe32_u32 *wc)
{
    struct wnd_class *c;
    ensure_builtin_classes();
    if (!wc || (unsigned long)wc[9] <= 0xffffu) {
        return 0;
    }
    c = find_class((const char *)(unsigned long)wc[9]);
    if (!c) {
        c = add_class((const char *)(unsigned long)wc[9], wc[1]);
    }
    if (!c) {
        return 0;
    }
    c->style = wc[0];
    c->proc = wc[1];
    c->extra = wc[2];
    c->wnd_extra = wc[3];
    c->instance = wc[4];
    c->icon = wc[5];
    c->cursor = wc[6];
    c->brush = wc[7];
    return 0xc000 + (pe32_u32)(c - classes);
}

static pe32_u32 PE32_WINAPI user_get_class_info_a(pe32_u32 instance,
                                                  const char *name,
                                                  pe32_u32 *wc)
{
    struct wnd_class *c;
    (void)instance;
    ensure_builtin_classes();
    c = find_class(name);
    if (!c || !wc) {
        return 0;
    }
    wc[0] = c->style;
    wc[1] = c->proc;
    wc[2] = c->extra;
    wc[3] = c->wnd_extra;
    wc[4] = c->instance;
    wc[5] = c->icon;
    wc[6] = c->cursor;
    wc[7] = c->brush;
    wc[8] = 0;
    wc[9] = (pe32_u32)(unsigned long)c->name;
    return 1;
}

static pe32_u32 PE32_WINAPI user_unregister_class_a(const char *name,
                                                    pe32_u32 instance)
{
    struct wnd_class *c = find_class(name);
    (void)instance;
    if (!c) {
        return 0;
    }
    c->used = 0;
    return 1;
}

/* ---- message queue ---------------------------------------------------- */

int pe32_wnd_post(pe32_u32 hwnd, pe32_u32 msg, pe32_u32 wparam, pe32_u32 lparam)
{
    pe32_u32 next = (queue_tail + 1) % MAX_QUEUE;
    if (next == queue_head) {
        return 0;
    }
    queue[queue_tail].hwnd = hwnd;
    queue[queue_tail].message = msg;
    queue[queue_tail].wparam = wparam;
    queue[queue_tail].lparam = lparam;
    queue[queue_tail].time = now_ms();
    queue[queue_tail].x = queue[queue_tail].y = 0;
    queue_tail = next;
    return 1;
}

static pe32_u32 PE32_WINAPI user_post_message_a(pe32_u32 hwnd, pe32_u32 msg,
                                                pe32_u32 wparam, pe32_u32 lparam)
{
    return (pe32_u32)pe32_wnd_post(hwnd, msg, wparam, lparam);
}

static pe32_u32 call_proc(pe32_u32 proc, pe32_u32 hwnd, pe32_u32 msg,
                          pe32_u32 wparam, pe32_u32 lparam)
{
#if defined(__i386__)
    return ((wndproc_fn)(unsigned long)proc)(hwnd, msg, wparam, lparam);
#else
    (void)proc; (void)hwnd; (void)msg; (void)wparam; (void)lparam;
    return 0;
#endif
}

static pe32_u32 PE32_WINAPI user_send_message_a(pe32_u32 hwnd, pe32_u32 msg,
                                                pe32_u32 wparam, pe32_u32 lparam)
{
    struct wnd *w = lookup(hwnd);
    /* The mission dialog asks an unattached list control (hwnd 0) for its
     * selection; answer with the mission chosen on the command line. */
    if (!w && msg == LB_GETCURSEL_MSG && mission_pick >= 0) {
        return (pe32_u32)mission_pick;
    }
    return w ? call_proc(w->proc, hwnd, msg, wparam, lparam) : 0;
}

static pe32_u32 PE32_WINAPI user_call_window_proc_a(pe32_u32 proc, pe32_u32 hwnd,
                                                    pe32_u32 msg, pe32_u32 wparam,
                                                    pe32_u32 lparam)
{
    return call_proc(proc, hwnd, msg, wparam, lparam);
}

static void pump_timers(void)
{
    int i;
    pe32_u32 t = now_ms();
    for (i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].used && (int)(t - timers[i].next) >= 0) {
            timers[i].next = t + timers[i].interval;
            if (timers[i].proc) {
                call_proc(timers[i].proc, timers[i].hwnd, WM_TIMER,
                          timers[i].id, t);
            } else {
                pe32_wnd_post(timers[i].hwnd, WM_TIMER, timers[i].id, 0);
            }
        }
    }
}

static int take_message(struct msg *out, pe32_u32 hwnd, int remove)
{
    pe32_u32 i;
    (void)hwnd;
    pump_timers();
    if (quit_pending) {
        out->hwnd = 0; out->message = WM_QUIT; out->wparam = quit_code;
        out->lparam = 0; out->time = now_ms(); out->x = out->y = 0;
        if (remove) {
            quit_pending = 0;
        }
        return 1;
    }
    i = queue_head;
    if (i == queue_tail) {
        return 0;
    }
    *out = queue[i];
    if (remove) {
        queue_head = (queue_head + 1) % MAX_QUEUE;
    }
    return 1;
}

static void store_msg(pe32_u32 *dst, const struct msg *m)
{
    dst[0] = m->hwnd; dst[1] = m->message; dst[2] = m->wparam;
    dst[3] = m->lparam; dst[4] = m->time; dst[5] = m->x; dst[6] = m->y;
}

static pe32_u32 PE32_WINAPI user_peek_message_a(pe32_u32 *dst, pe32_u32 hwnd,
                                                pe32_u32 min, pe32_u32 max,
                                                pe32_u32 flags)
{
    struct msg m;
    (void)min; (void)max;
    if (!take_message(&m, hwnd, (int)(flags & PM_REMOVE))) {
        return 0;
    }
    store_msg(dst, &m);
    return 1;
}

static pe32_u32 PE32_WINAPI user_get_message_a(pe32_u32 *dst, pe32_u32 hwnd,
                                               pe32_u32 min, pe32_u32 max)
{
    struct msg m;
    pe32_u32 idle = 0;
    (void)min; (void)max;
    for (;;) {
        if (take_message(&m, hwnd, 1)) {
            store_msg(dst, &m);
            return m.message != WM_QUIT;
        }
        if (auto_accept && ++idle > 3 && modal_dialog &&
            lookup(modal_dialog)) {
            pe32_u32 cmd = 1, k;                /* IDOK */
            idle = 0;
            for (k = 0; k < 256; k++) {         /* main menu: MAINMISSION */
                if (windows[k].used && windows[k].parent == modal_dialog &&
                    windows[k].id == 1136) {    /* main menu has MAINQUIT */
                    cmd = 3;
                }
            }
            for (k = 0; k < 256; k++) {         /* mission list: native screen */
                if (ui_hook && mission_choice < 0 && windows[k].used &&
                    windows[k].parent == modal_dialog && windows[k].id == 1057) {
                    int r = ui_hook(1, mission_pick >= 0 ? mission_pick : 0);
                    if (r >= 0) {
                        mission_pick = r;
                    } else if (r == -1) {
                        cmd = 2;                /* IDCANCEL: back to the menu */
                    }
                    break;
                }
            }
            if (mission_choice >= 0) {          /* mission list: pick one */
                for (k = 0; k < 256; k++) {
                    pe32_u32 lb = HWND_BASE + (pe32_u32)k;
                    if (windows[k].used && windows[k].parent == modal_dialog &&
                        windows[k].cls && str_eq_ci(windows[k].cls->name, "LISTBOX") &&
                        pe32_wnd_listbox_count(lb) > 0) {
                        int count = pe32_wnd_listbox_count(lb);
                        int pick = mission_choice < count ? mission_choice : count - 1;
                        pe32_wnd_listbox_select(lb, pick);
                        /* LBN_SELCHANGE so the dialog updates its details. */
                        pe32_wnd_post(modal_dialog, WM_COMMAND,
                                      (1u << 16) | windows[k].id, lb);
                        mission_choice = -1;
                    }
                }
            }
            pe32_wnd_post(modal_dialog, WM_COMMAND, cmd, 0);
            continue;
        }
        pe32_user_poll_input();
        if (idle_hook) {
            idle_hook(10);
        } else {
            pe32_win32_sleep(10);
        }
    }
}

static pe32_u32 PE32_WINAPI user_dispatch_message_a(const pe32_u32 *m)
{
    struct wnd *w = lookup(m[0]);
    if (m[1] == WM_TIMER && m[3]) {
        return call_proc(m[3], m[0], m[1], m[2], now_ms());
    }
    return w ? call_proc(w->proc, m[0], m[1], m[2], m[3]) : 0;
}

static pe32_u32 PE32_WINAPI user_translate_message(const pe32_u32 *m)
{
    (void)m;
    return 0;
}

static pe32_u32 PE32_WINAPI user_post_quit_message(pe32_u32 code)
{
    quit_pending = 1;
    quit_code = code;
    return 0;
}

static pe32_u32 PE32_WINAPI user_get_message_pos(void) { return 0; }
static pe32_u32 PE32_WINAPI user_get_message_time(void) { return now_ms(); }

static pe32_u32 PE32_WINAPI user_set_timer(pe32_u32 hwnd, pe32_u32 id,
                                           pe32_u32 interval, pe32_u32 proc)
{
    int i, free_slot = -1;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].used && timers[i].hwnd == hwnd && timers[i].id == id) {
            free_slot = i;
            break;
        }
        if (!timers[i].used && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return 0;
    }
    timers[free_slot].used = 1;
    timers[free_slot].hwnd = hwnd;
    timers[free_slot].id = hwnd ? id : 0x100 + (pe32_u32)free_slot;
    timers[free_slot].interval = interval ? interval : 1;
    timers[free_slot].proc = proc;
    timers[free_slot].next = now_ms() + timers[free_slot].interval;
    return timers[free_slot].id;
}

static pe32_u32 PE32_WINAPI user_kill_timer(pe32_u32 hwnd, pe32_u32 id)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].used && timers[i].hwnd == hwnd && timers[i].id == id) {
            timers[i].used = 0;
            return 1;
        }
    }
    return 0;
}

/* ---- windows ---------------------------------------------------------- */

/* ---- minimal LISTBOX: item text/data and a current selection ----------- */

enum { MAX_LISTBOXES = 8, LB_ITEMS = 64, LB_TEXT = 64,
       LB_ADDSTRING = 0x180, LB_INSERTSTRING = 0x181, LB_DELETESTRING = 0x182,
       LB_RESETCONTENT = 0x184, LB_SETCURSEL = 0x186, LB_GETCURSEL = 0x188,
       LB_GETTEXT = 0x189, LB_GETTEXTLEN = 0x18a, LB_GETCOUNT = 0x18b,
       LB_FINDSTRING = 0x18c, LB_GETITEMDATA = 0x199, LB_SETITEMDATA = 0x19a,
       LB_ERR = 0xffffffffu };

struct listbox {
    pe32_u32 hwnd;
    int count, cursel;
    char text[LB_ITEMS][LB_TEXT];
    pe32_u32 data[LB_ITEMS];
};

static struct listbox listboxes[MAX_LISTBOXES];

static struct listbox *listbox_for(pe32_u32 hwnd, int create)
{
    int i, free_slot = -1;
    for (i = 0; i < (int)MAX_LISTBOXES; i++) {
        if (listboxes[i].hwnd == hwnd) {
            return &listboxes[i];
        }
        if (!listboxes[i].hwnd && free_slot < 0) {
            free_slot = i;
        }
    }
    if (!create || free_slot < 0) {
        return 0;
    }
    listboxes[free_slot].hwnd = hwnd;
    listboxes[free_slot].count = 0;
    listboxes[free_slot].cursel = -1;
    return &listboxes[free_slot];
}

int pe32_wnd_listbox_count(pe32_u32 hwnd)
{
    struct listbox *lb = listbox_for(hwnd, 0);
    return lb ? lb->count : 0;
}

static pe32_u32 listbox_message(pe32_u32 hwnd, pe32_u32 msg, pe32_u32 wparam,
                                pe32_u32 lparam);

void pe32_wnd_listbox_select(pe32_u32 hwnd, int index)
{
    listbox_message(hwnd, LB_SETCURSEL, (pe32_u32)index, 0);
}

static pe32_u32 listbox_message(pe32_u32 hwnd, pe32_u32 msg, pe32_u32 wparam,
                                pe32_u32 lparam)
{
    struct listbox *lb = listbox_for(hwnd, 1);
    int i, at;
    if (!lb) {
        return LB_ERR;
    }
    switch (msg) {
    case LB_ADDSTRING:
    case LB_INSERTSTRING:
        if (lb->count >= (int)LB_ITEMS) {
            return LB_ERR;
        }
        at = msg == LB_ADDSTRING || (int)wparam < 0 || (int)wparam > lb->count
             ? lb->count : (int)wparam;
        for (i = lb->count; i > at; i--) {
            str_copy(lb->text[i], lb->text[i - 1], LB_TEXT);
            lb->data[i] = lb->data[i - 1];
        }
        str_copy(lb->text[at], (const char *)(unsigned long)lparam, LB_TEXT);
        lb->data[at] = 0;
        lb->count++;
        if (lb->cursel >= at) {
            lb->cursel++;
        }
        return (pe32_u32)at;
    case LB_DELETESTRING:
        if ((int)wparam < 0 || (int)wparam >= lb->count) {
            return LB_ERR;
        }
        for (i = (int)wparam; i + 1 < lb->count; i++) {
            str_copy(lb->text[i], lb->text[i + 1], LB_TEXT);
            lb->data[i] = lb->data[i + 1];
        }
        lb->count--;
        if (lb->cursel >= lb->count) {
            lb->cursel = -1;
        }
        return (pe32_u32)lb->count;
    case LB_RESETCONTENT:
        lb->count = 0;
        lb->cursel = -1;
        return 0;
    case LB_SETCURSEL:
        if ((int)wparam < 0 || (int)wparam >= lb->count) {
            lb->cursel = -1;
            return LB_ERR;
        }
        lb->cursel = (int)wparam;
        return wparam;
    case LB_GETCURSEL: return (pe32_u32)lb->cursel;
    case LB_GETCOUNT: return (pe32_u32)lb->count;
    case LB_GETTEXTLEN:
        return (int)wparam >= 0 && (int)wparam < lb->count
               ? (pe32_u32)str_len(lb->text[wparam]) : LB_ERR;
    case LB_GETTEXT:
        if ((int)wparam < 0 || (int)wparam >= lb->count || !lparam) {
            return LB_ERR;
        }
        str_copy((char *)(unsigned long)lparam, lb->text[wparam], LB_TEXT);
        return (pe32_u32)str_len(lb->text[wparam]);
    case LB_FINDSTRING:
        for (i = 0; i < lb->count; i++) {
            if (str_eq_ci(lb->text[i], (const char *)(unsigned long)lparam)) {
                return (pe32_u32)i;
            }
        }
        return LB_ERR;
    case LB_GETITEMDATA:
        return (int)wparam >= 0 && (int)wparam < lb->count ? lb->data[wparam] : LB_ERR;
    case LB_SETITEMDATA:
        if ((int)wparam < 0 || (int)wparam >= lb->count) {
            return LB_ERR;
        }
        lb->data[wparam] = lparam;
        return 1;
    default:
        return 0;
    }
}

static pe32_u32 PE32_WINAPI def_window_proc(pe32_u32 hwnd, pe32_u32 msg,
                                            pe32_u32 wparam, pe32_u32 lparam)
{
    struct wnd *w = lookup(hwnd);
    if (!w) {
        return 0;
    }
    if (w->cls && str_eq_ci(w->cls->name, "LISTBOX") && msg >= 0x180 && msg <= 0x1a0) {
        return listbox_message(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_NCCREATE: return 1;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: user_destroy_window(hwnd); return 0;
    case WM_SETTEXT:
        str_copy(w->text, (const char *)(unsigned long)lparam, TEXT_MAX);
        return 1;
    case WM_GETTEXTLENGTH: return str_len(w->text);
    case WM_GETTEXT:
        if (!wparam) {
            return 0;
        }
        str_copy((char *)(unsigned long)lparam, w->text, wparam);
        return str_len((char *)(unsigned long)lparam);
    default: return 0;
    }
}

static pe32_u32 PE32_WINAPI user_def_window_proc_a(pe32_u32 hwnd, pe32_u32 msg,
                                                   pe32_u32 wparam,
                                                   pe32_u32 lparam)
{
    return def_window_proc(hwnd, msg, wparam, lparam);
}

static pe32_u32 create_window(pe32_u32 exstyle, const char *cls, const char *title,
                              pe32_u32 style, int x, int y, int cx, int cy,
                              pe32_u32 parent, pe32_u32 menu, pe32_u32 instance,
                              pe32_u32 param)
{
    struct wnd_class *c;
    struct wnd *w;
    pe32_u32 hwnd, cs[12], cbt[2], hook, i;
    ensure_builtin_classes();
    c = find_class(cls);
    if (!c) {
        return 0;
    }
    for (i = 0; i < MAX_WINDOWS && windows[i].used; i++) {
    }
    if (i == MAX_WINDOWS) {
        return 0;
    }
    w = &windows[i];
    for (hwnd = 0; hwnd < sizeof *w; hwnd++) {
        ((char *)w)[hwnd] = 0;
    }
    hwnd = HWND_BASE + i;
    w->used = 1;
    w->cls = c;
    w->proc = c->proc;
    w->style = style;
    w->exstyle = exstyle;
    w->parent = parent;
    w->instance = instance;
    w->id = (style & WS_CHILD) ? menu : 0;
    w->rect[0] = x == (int)0x80000000 ? 0 : x;
    w->rect[1] = y == (int)0x80000000 ? 0 : y;
    w->rect[2] = w->rect[0] + (cx == (int)0x80000000 ? 640 : cx);
    w->rect[3] = w->rect[1] + (cy == (int)0x80000000 ? 480 : cy);
    w->enabled = !(style & WS_DISABLED);
    str_copy(w->text, title, TEXT_MAX);
    cs[0] = param; cs[1] = instance; cs[2] = menu; cs[3] = parent;
    cs[4] = (pe32_u32)cy; cs[5] = (pe32_u32)cx; cs[6] = (pe32_u32)y;
    cs[7] = (pe32_u32)x; cs[8] = style;
    cs[9] = (pe32_u32)(unsigned long)title;
    cs[10] = (pe32_u32)(unsigned long)cls; cs[11] = exstyle;
    hook = pe32_user_hook_proc(5 /* WH_CBT */);
    if (hook) {
        cbt[0] = (pe32_u32)(unsigned long)cs;
        cbt[1] = 0;
        (void)cbt;
#if defined(__i386__)
        if (((hookproc_fn)(unsigned long)hook)(HCBT_CREATEWND, hwnd,
                                               (pe32_u32)(unsigned long)cbt)) {
            w->used = 0;
            return 0;
        }
#endif
    }
    if (!call_proc(w->proc, hwnd, WM_NCCREATE, 0, (pe32_u32)(unsigned long)cs) ||
        call_proc(w->proc, hwnd, WM_CREATE, 0, (pe32_u32)(unsigned long)cs) ==
            0xffffffffu) {
        user_destroy_window(hwnd);
        return 0;
    }
    if (style & WS_VISIBLE) {
        w->visible = 1;
        call_proc(w->proc, hwnd, WM_SHOWWINDOW, 1, 0);
    }
    return hwnd;
}

static pe32_u32 PE32_WINAPI user_create_window_ex_a(
    pe32_u32 exstyle, const char *cls, const char *title, pe32_u32 style,
    int x, int y, int cx, int cy, pe32_u32 parent, pe32_u32 menu,
    pe32_u32 instance, pe32_u32 param)
{
    return create_window(exstyle, cls, title, style, x, y, cx, cy, parent, menu,
                         instance, param);
}

static pe32_u32 PE32_WINAPI user_destroy_window(pe32_u32 hwnd)
{
    struct wnd *w = lookup(hwnd);
    pe32_u32 i;
    if (!w) {
        return 0;
    }
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && (windows[i].style & WS_CHILD) &&
            windows[i].parent == hwnd) {
            user_destroy_window(HWND_BASE + i);
        }
    }
    call_proc(w->proc, hwnd, WM_DESTROY, 0, 0);
    call_proc(w->proc, hwnd, WM_NCDESTROY, 0, 0);
    if (focus == hwnd) focus = 0;
    if (active == hwnd) active = 0;
    if (capture == hwnd) capture = 0;
    w->used = 0;
    return 1;
}

static pe32_u32 *slot_for(struct wnd *w, int index)
{
    static pe32_u32 scratch;
    switch (index) {
    case GWL_WNDPROC: return &w->proc;
    case GWL_HINSTANCE: return &w->instance;
    case GWL_HWNDPARENT: return &w->parent;
    case GWL_STYLE: return &w->style;
    case GWL_EXSTYLE: return &w->exstyle;
    case GWL_USERDATA: return &w->user_data;
    case GWL_ID: return &w->id;
    default:
        if (index >= 0 && index < 32 && !(index & 3)) {
            return &w->extra[index / 4];
        }
        scratch = 0;
        return &scratch;
    }
}

static pe32_u32 PE32_WINAPI user_get_window_long_a(pe32_u32 hwnd, int index)
{
    struct wnd *w = lookup(hwnd);
    return w ? *slot_for(w, index) : 0;
}

static pe32_u32 PE32_WINAPI user_set_window_long_a(pe32_u32 hwnd, int index,
                                                   pe32_u32 value)
{
    struct wnd *w = lookup(hwnd);
    pe32_u32 old, *slot;
    if (!w) {
        return 0;
    }
    slot = slot_for(w, index);
    old = *slot;
    *slot = value;
    return old;
}

static pe32_u32 PE32_WINAPI user_set_prop_a(pe32_u32 hwnd, const char *name,
                                            pe32_u32 value)
{
    struct wnd *w = lookup(hwnd);
    int i, free_slot = -1;
    if (!w || !name) {
        return 0;
    }
    for (i = 0; i < MAX_PROPS; i++) {
        if (w->props[i].name[0] && str_eq_ci(w->props[i].name, name)) {
            w->props[i].value = value;
            return 1;
        }
        if (!w->props[i].name[0] && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) {
        return 0;
    }
    str_copy(w->props[free_slot].name, name, sizeof w->props[0].name);
    w->props[free_slot].value = value;
    return 1;
}

static pe32_u32 PE32_WINAPI user_get_prop_a(pe32_u32 hwnd, const char *name)
{
    struct wnd *w = lookup(hwnd);
    int i;
    if (!w || !name) {
        return 0;
    }
    for (i = 0; i < MAX_PROPS; i++) {
        if (w->props[i].name[0] && str_eq_ci(w->props[i].name, name)) {
            return w->props[i].value;
        }
    }
    return 0;
}

static pe32_u32 PE32_WINAPI user_remove_prop_a(pe32_u32 hwnd, const char *name)
{
    struct wnd *w = lookup(hwnd);
    int i;
    pe32_u32 v = user_get_prop_a(hwnd, name);
    if (!w) {
        return 0;
    }
    for (i = 0; i < MAX_PROPS; i++) {
        if (w->props[i].name[0] && str_eq_ci(w->props[i].name, name)) {
            w->props[i].name[0] = 0;
        }
    }
    return v;
}

static pe32_u32 PE32_WINAPI user_get_parent(pe32_u32 hwnd)
{
    struct wnd *w = lookup(hwnd);
    return w ? w->parent : 0;
}

static pe32_u32 PE32_WINAPI user_is_window(pe32_u32 hwnd)
{
    return lookup(hwnd) != 0;
}

static pe32_u32 PE32_WINAPI user_is_window_visible(pe32_u32 hwnd)
{
    struct wnd *w = lookup(hwnd);
    return w && w->visible;
}

static pe32_u32 PE32_WINAPI user_is_window_enabled(pe32_u32 hwnd)
{
    struct wnd *w = lookup(hwnd);
    return w && w->enabled;
}

static pe32_u32 PE32_WINAPI user_enable_window(pe32_u32 hwnd, pe32_u32 enable)
{
    struct wnd *w = lookup(hwnd);
    pe32_u32 old;
    if (!w) {
        return 0;
    }
    old = !w->enabled;
    w->enabled = enable != 0;
    return old;
}

static pe32_u32 PE32_WINAPI user_show_window(pe32_u32 hwnd, pe32_u32 cmd)
{
    struct wnd *w = lookup(hwnd);
    pe32_u32 old;
    if (!w) {
        return 0;
    }
    old = w->visible;
    w->visible = cmd != 0;
    call_proc(w->proc, hwnd, WM_SHOWWINDOW, w->visible, 0);
    return old;
}

static pe32_u32 PE32_WINAPI user_get_window_rect(pe32_u32 hwnd, int *r)
{
    struct wnd *w = lookup(hwnd);
    if (!w || !r) {
        return 0;
    }
    r[0] = w->rect[0]; r[1] = w->rect[1]; r[2] = w->rect[2]; r[3] = w->rect[3];
    return 1;
}

static pe32_u32 PE32_WINAPI user_get_client_rect(pe32_u32 hwnd, int *r)
{
    struct wnd *w = lookup(hwnd);
    if (!w || !r) {
        return 0;
    }
    r[0] = r[1] = 0;
    r[2] = w->rect[2] - w->rect[0];
    r[3] = w->rect[3] - w->rect[1];
    return 1;
}

static pe32_u32 PE32_WINAPI user_set_window_pos(pe32_u32 hwnd, pe32_u32 after,
                                                int x, int y, int cx, int cy,
                                                pe32_u32 flags)
{
    struct wnd *w = lookup(hwnd);
    (void)after;
    if (!w) {
        return 0;
    }
    if (!(flags & 2)) { /* !SWP_NOMOVE */
        int dx = w->rect[2] - w->rect[0], dy = w->rect[3] - w->rect[1];
        w->rect[0] = x; w->rect[1] = y; w->rect[2] = x + dx; w->rect[3] = y + dy;
    }
    if (!(flags & 1)) { /* !SWP_NOSIZE */
        w->rect[2] = w->rect[0] + cx;
        w->rect[3] = w->rect[1] + cy;
        call_proc(w->proc, hwnd, WM_SIZE, 0,
                  ((pe32_u32)cy << 16) | ((pe32_u32)cx & 0xffff));
    }
    if (flags & 0x40) w->visible = 1;
    if (flags & 0x80) w->visible = 0;
    return 1;
}

static pe32_u32 PE32_WINAPI user_get_window_text_a(pe32_u32 hwnd, char *buf,
                                                   pe32_u32 max)
{
    struct wnd *w = lookup(hwnd);
    if (!w || !buf || !max) {
        return 0;
    }
    str_copy(buf, w->text, max);
    return str_len(buf);
}

static pe32_u32 PE32_WINAPI user_get_window_text_length_a(pe32_u32 hwnd)
{
    struct wnd *w = lookup(hwnd);
    return w ? str_len(w->text) : 0;
}

static pe32_u32 PE32_WINAPI user_set_window_text_a(pe32_u32 hwnd, const char *t)
{
    return user_send_message_a(hwnd, WM_SETTEXT, 0, (pe32_u32)(unsigned long)t);
}

static pe32_u32 PE32_WINAPI user_get_dlg_item(pe32_u32 hwnd, pe32_u32 id)
{
    pe32_u32 i;
    for (i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].parent == hwnd &&
            (windows[i].style & WS_CHILD) && windows[i].id == id) {
            return HWND_BASE + i;
        }
    }
    return 0;
}

static pe32_u32 PE32_WINAPI user_get_dlg_ctrl_id(pe32_u32 hwnd)
{
    struct wnd *w = lookup(hwnd);
    return w ? w->id : 0;
}

static pe32_u32 PE32_WINAPI user_send_dlg_item_message_a(
    pe32_u32 hwnd, pe32_u32 id, pe32_u32 msg, pe32_u32 wparam, pe32_u32 lparam)
{
    return user_send_message_a(user_get_dlg_item(hwnd, id), msg, wparam, lparam);
}

static pe32_u32 PE32_WINAPI user_get_window(pe32_u32 hwnd, pe32_u32 cmd)
{
    pe32_u32 i;
    struct wnd *w = lookup(hwnd);
    if (cmd == 4 && w) { /* GW_OWNER */
        return w->owner;
    }
    if (cmd == 5 && w) { /* GW_CHILD */
        for (i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].used && windows[i].parent == hwnd) {
                return HWND_BASE + i;
            }
        }
    }
    return 0;
}

static pe32_u32 PE32_WINAPI user_get_top_window(pe32_u32 hwnd)
{
    return user_get_window(hwnd, 5);
}

static pe32_u32 PE32_WINAPI user_set_focus(pe32_u32 hwnd)
{
    pe32_u32 old = focus;
    focus = hwnd;
    return old;
}

/* Keys go to the window the game bound DirectDraw to (the cockpit), else to
 * the focus/active window. */
/* Zero-based index of the mission to pick when auto-accepting the list. */
void pe32_wnd_set_mission(int index) { mission_choice = index; mission_pick = index; }

void pe32_wnd_set_game_window(pe32_u32 hwnd)
{
    game_window = hwnd;
    if (hwnd && lookup(hwnd)) {
        /* A full-screen DirectDraw app is the foreground application:
         * tell it so, or it treats itself as paused/inactive. */
        active = hwnd;
        focus = hwnd;
        pe32_wnd_post(hwnd, 0x1c /* WM_ACTIVATEAPP */, 1, 0);
        pe32_wnd_post(hwnd, 0x06 /* WM_ACTIVATE */, 1, 0);
        pe32_wnd_post(hwnd, 0x07 /* WM_SETFOCUS */, 0, 0);
    }
}

pe32_u32 pe32_wnd_input_target(void)
{
    if (game_window && lookup(game_window)) {
        return game_window;
    }
    return focus ? focus : active;
}

static pe32_u32 PE32_WINAPI user_get_focus(void) { return focus; }

static pe32_u32 PE32_WINAPI user_set_active_window(pe32_u32 hwnd)
{
    pe32_u32 old = active;
    active = hwnd;
    return old;
}

static pe32_u32 PE32_WINAPI user_get_active_window(void) { return active; }

static pe32_u32 PE32_WINAPI user_set_capture(pe32_u32 hwnd)
{
    pe32_u32 old = capture;
    capture = hwnd;
    return old;
}

static pe32_u32 PE32_WINAPI user_release_capture(void)
{
    capture = 0;
    return 1;
}

static pe32_u32 PE32_WINAPI user_get_capture(void) { return capture; }
static pe32_u32 PE32_WINAPI user_get_desktop_window(void) { return HWND_BASE - 4; }
/* stdcall callee pops its arguments, so constant-returning stubs must declare
 * the exact parameter count of the API they stand in for. */
static pe32_u32 PE32_WINAPI user_one_arg_one(pe32_u32 a) { (void)a; return 1; }
static pe32_u32 PE32_WINAPI user_one_arg_zero(pe32_u32 a) { (void)a; return 0; }
static pe32_u32 PE32_WINAPI user_two_args_one(pe32_u32 a, pe32_u32 b)
{
    (void)a; (void)b;
    return 1;
}
static pe32_u32 PE32_WINAPI user_three_args_one(pe32_u32 a, pe32_u32 b,
                                                pe32_u32 c)
{
    (void)a; (void)b; (void)c;
    return 1;
}

/* ---- dialogs ---------------------------------------------------------- */

static const unsigned char *skip_sz_or_ordinal(const unsigned char *p)
{
    if (p[0] == 0xff && p[1] == 0xff) {
        return p + 4;
    }
    while (p[0] || p[1]) {
        p += 2;
    }
    return p + 2;
}

static void wide_to_ascii(char *out, const unsigned char *p, pe32_u32 max)
{
    pe32_u32 i = 0;
    while ((p[0] || p[1]) && i + 1 < max) {
        out[i++] = (char)p[0];
        p += 2;
    }
    out[i] = 0;
}

static pe32_u32 rd16(const unsigned char *p) { return p[0] | (pe32_u32)p[1] << 8; }
static pe32_u32 rd32(const unsigned char *p) { return rd16(p) | rd16(p + 2) << 16; }

static const unsigned char *align4(const unsigned char *base,
                                   const unsigned char *p)
{
    return base + (((unsigned long)(p - base) + 3) & ~3ul);
}

static const char *builtin_class_name(unsigned ordinal)
{
    switch (ordinal) {
    case 0x80: return "BUTTON";
    case 0x81: return "EDIT";
    case 0x82: return "STATIC";
    case 0x83: return "LISTBOX";
    case 0x84: return "SCROLLBAR";
    case 0x85: return "COMBOBOX";
    default: return "STATIC";
    }
}

static pe32_u32 PE32_WINAPI user_create_dialog_indirect_param_a(
    pe32_u32 instance, const unsigned char *tpl, pe32_u32 parent,
    pe32_u32 dlgproc, pe32_u32 init)
{
    const unsigned char *p = tpl;
    pe32_u32 style, exstyle, count, hwnd, i, first_control = 0;
    int x, y, cx, cy, extended;
    char title[TEXT_MAX], cls[48], text[TEXT_MAX];
    (void)dlgproc;
    if (!tpl) {
        return 0;
    }
    /* DLGTEMPLATEEX starts with version 1 and signature 0xffff. */
    extended = rd16(p) == 1 && rd16(p + 2) == 0xffff;
    if (extended) {
        exstyle = rd32(p + 8); style = rd32(p + 12); count = rd16(p + 16);
        x = (short)rd16(p + 18); y = (short)rd16(p + 20);
        cx = (short)rd16(p + 22); cy = (short)rd16(p + 24);
        p += 26;
    } else {
        style = rd32(p); exstyle = rd32(p + 4); count = rd16(p + 8);
        x = (short)rd16(p + 10); y = (short)rd16(p + 12);
        cx = (short)rd16(p + 14); cy = (short)rd16(p + 16);
        p += 18;
    }
    p = skip_sz_or_ordinal(p);          /* menu */
    p = skip_sz_or_ordinal(p);          /* class */
    wide_to_ascii(title, p, sizeof title);
    p = skip_sz_or_ordinal(p);          /* title */
    if (style & 0x40) {                 /* DS_SETFONT */
        p += extended ? 6 : 2;          /* size [+ weight, italic, charset] */
        p = skip_sz_or_ordinal(p);
    }
    /* Dialog units to pixels with the classic 8x16 system font ratio. */
    hwnd = create_window(exstyle, "#32770", title, style & ~(pe32_u32)WS_VISIBLE,
                         x * 2, y * 2, cx * 2, cy * 2, parent, 0, instance, init);
    if (!hwnd) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        pe32_u32 istyle, iexstyle, id, child;
        int ix, iy, icx, icy;
        const char *class_name;
        p = align4(tpl, p);
        if (extended) {
            iexstyle = rd32(p + 4); istyle = rd32(p + 8);
            ix = (short)rd16(p + 12); iy = (short)rd16(p + 14);
            icx = (short)rd16(p + 16); icy = (short)rd16(p + 18);
            id = rd32(p + 20);
            p += 24;
        } else {
            istyle = rd32(p); iexstyle = rd32(p + 4);
            ix = (short)rd16(p + 8); iy = (short)rd16(p + 10);
            icx = (short)rd16(p + 12); icy = (short)rd16(p + 14);
            id = rd16(p + 16);
            p += 18;
        }
        if (p[0] == 0xff && p[1] == 0xff) {
            class_name = builtin_class_name(rd16(p + 2));
        } else {
            wide_to_ascii(cls, p, sizeof cls);
            class_name = cls;
        }
        p = skip_sz_or_ordinal(p);
        if (p[0] == 0xff && p[1] == 0xff) {
            text[0] = 0;
        } else {
            wide_to_ascii(text, p, sizeof text);
        }
        p = skip_sz_or_ordinal(p);
        p += 2 + rd16(p);               /* creation data */
        child = create_window(iexstyle, class_name, text,
                              istyle | WS_CHILD, ix * 2, iy * 2, icx * 2,
                              icy * 2, hwnd, id, instance, 0);
        if (child && !first_control) {
            first_control = child;
        }
    }
    if (call_proc(windows[hwnd - HWND_BASE].proc, hwnd, WM_INITDIALOG,
                  first_control, init) && first_control) {
        focus = first_control;
    }
    active = hwnd;
    modal_dialog = hwnd;
    if (style & WS_VISIBLE) {
        user_show_window(hwnd, 1);
    }
    return hwnd;
}

static pe32_u32 PE32_WINAPI user_end_dialog(pe32_u32 hwnd, pe32_u32 result)
{
    (void)result;
    if (lookup(hwnd)) {
        lookup(hwnd)->ended = 1;
    }
    if (hwnd == modal_dialog) {
        pe32_u32 k;
        modal_dialog = 0;               /* fall back to an older live dialog */
        for (k = 0; k < 256; k++) {
            if (windows[k].used && !windows[k].ended && HWND_BASE + k != hwnd &&
                windows[k].cls && windows[k].cls->name[0] == '#') {
                modal_dialog = HWND_BASE + k;
            }
        }
    }
    return 1;
}

/* ---- rectangles ------------------------------------------------------- */

static pe32_u32 PE32_WINAPI user_set_rect(int *r, int l, int t, int rr, int b)
{
    r[0] = l; r[1] = t; r[2] = rr; r[3] = b;
    return 1;
}

static pe32_u32 PE32_WINAPI user_set_rect_empty(int *r)
{
    return user_set_rect(r, 0, 0, 0, 0);
}

static pe32_u32 PE32_WINAPI user_copy_rect(int *to, const int *from)
{
    return user_set_rect(to, from[0], from[1], from[2], from[3]);
}

static pe32_u32 PE32_WINAPI user_equal_rect(const int *a, const int *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static pe32_u32 PE32_WINAPI user_offset_rect(int *r, int dx, int dy)
{
    r[0] += dx; r[2] += dx; r[1] += dy; r[3] += dy;
    return 1;
}

static pe32_u32 PE32_WINAPI user_inflate_rect(int *r, int dx, int dy)
{
    r[0] -= dx; r[2] += dx; r[1] -= dy; r[3] += dy;
    return 1;
}

static pe32_u32 PE32_WINAPI user_pt_in_rect(const int *r, int x, int y)
{
    return x >= r[0] && x < r[2] && y >= r[1] && y < r[3];
}

static pe32_u32 PE32_WINAPI user_client_to_screen(pe32_u32 hwnd, int *pt)
{
    struct wnd *w = lookup(hwnd);
    if (w) {
        pt[0] += w->rect[0];
        pt[1] += w->rect[1];
    }
    return 1;
}

static pe32_u32 PE32_WINAPI user_screen_to_client(pe32_u32 hwnd, int *pt)
{
    struct wnd *w = lookup(hwnd);
    if (w) {
        pt[0] -= w->rect[0];
        pt[1] -= w->rect[1];
    }
    return 1;
}

#if defined(__i386__)
static int names_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
#endif

enum pe32_link_status pe32_wnd_resolve(const char *name, pe32_u32 *address)
{
#if defined(__i386__)
#define E(legacy, impl) \
    if (names_equal(name, legacy)) { \
        *address = (pe32_u32)(unsigned long)&impl; \
        return PE32_LINK_OK; \
    }
    E("RegisterClassA", user_register_class_a)
    E("GetClassInfoA", user_get_class_info_a)
    E("UnregisterClassA", user_unregister_class_a)
    E("CreateWindowExA", user_create_window_ex_a)
    E("DestroyWindow", user_destroy_window)
    E("DefWindowProcA", user_def_window_proc_a)
    E("CallWindowProcA", user_call_window_proc_a)
    E("SendMessageA", user_send_message_a)
    E("PostMessageA", user_post_message_a)
    E("PeekMessageA", user_peek_message_a)
    E("GetMessageA", user_get_message_a)
    E("DispatchMessageA", user_dispatch_message_a)
    E("TranslateMessage", user_translate_message)
    E("PostQuitMessage", user_post_quit_message)
    E("GetMessagePos", user_get_message_pos)
    E("GetMessageTime", user_get_message_time)
    E("SetTimer", user_set_timer)
    E("KillTimer", user_kill_timer)
    E("GetWindowLongA", user_get_window_long_a)
    E("SetWindowLongA", user_set_window_long_a)
    E("SetPropA", user_set_prop_a)
    E("GetPropA", user_get_prop_a)
    E("RemovePropA", user_remove_prop_a)
    E("GetParent", user_get_parent)
    E("IsWindow", user_is_window)
    E("IsWindowVisible", user_is_window_visible)
    E("IsWindowEnabled", user_is_window_enabled)
    E("EnableWindow", user_enable_window)
    E("ShowWindow", user_show_window)
    E("GetWindowRect", user_get_window_rect)
    E("GetClientRect", user_get_client_rect)
    E("SetWindowPos", user_set_window_pos)
    E("GetWindowTextA", user_get_window_text_a)
    E("GetWindowTextLengthA", user_get_window_text_length_a)
    E("SetWindowTextA", user_set_window_text_a)
    E("GetDlgItem", user_get_dlg_item)
    E("GetDlgCtrlID", user_get_dlg_ctrl_id)
    E("SendDlgItemMessageA", user_send_dlg_item_message_a)
    E("GetWindow", user_get_window)
    E("GetTopWindow", user_get_top_window)
    E("SetFocus", user_set_focus)
    E("GetFocus", user_get_focus)
    E("SetActiveWindow", user_set_active_window)
    E("GetActiveWindow", user_get_active_window)
    E("GetForegroundWindow", user_get_active_window)
    E("SetForegroundWindow", user_one_arg_one)
    E("GetLastActivePopup", user_get_parent)
    E("SetCapture", user_set_capture)
    E("ReleaseCapture", user_release_capture)
    E("GetCapture", user_get_capture)
    E("GetDesktopWindow", user_get_desktop_window)
    E("CreateDialogIndirectParamA", user_create_dialog_indirect_param_a)
    E("EndDialog", user_end_dialog)
    E("SetRect", user_set_rect)
    E("SetRectEmpty", user_set_rect_empty)
    E("CopyRect", user_copy_rect)
    E("EqualRect", user_equal_rect)
    E("OffsetRect", user_offset_rect)
    E("InflateRect", user_inflate_rect)
    E("PtInRect", user_pt_in_rect)
    E("ClientToScreen", user_client_to_screen)
    E("ScreenToClient", user_screen_to_client)
    E("InvalidateRect", user_three_args_one)
    E("UpdateWindow", user_one_arg_one)
    E("ValidateRect", user_two_args_one)
    E("IsIconic", user_one_arg_zero)
    E("ShowCursor", user_one_arg_one)
    E("SetCursor", user_one_arg_zero)
#undef E
#else
    (void)name;
    (void)address;
#endif
    return PE32_LINK_MISSING;
}
