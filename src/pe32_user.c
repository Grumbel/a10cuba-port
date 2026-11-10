/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_user.h"
#include "pe32_winmm.h"
#include "pe32_res.h"

enum { HOOK_TYPES = 16, HOOK_BASE = 0x00a10000, MESSAGE_BASE = 0xc000 };

static pe32_u32 hook_procs[HOOK_TYPES + 1]; /* indexed by type + 1 */
static pe32_u32 next_message = MESSAGE_BASE;
static pe32_u32 next_handle = 0x00b10000;

pe32_u32 pe32_user_hook_proc(pe32_u32 type)
{
    return type + 1 <= HOOK_TYPES ? hook_procs[type + 1] : 0;
}

/* Hook types are -1 (WH_MSGFILTER) .. 14; the handle encodes type + 1. */
pe32_u32 PE32_WINAPI pe32_user_set_windows_hook_ex(pe32_u32 type, pe32_u32 proc,
                                                   pe32_u32 module,
                                                   pe32_u32 thread)
{
    (void)module;
    (void)thread;
    if (type + 1 > HOOK_TYPES || !proc) {
        return 0;
    }
    hook_procs[type + 1] = proc;
    return HOOK_BASE + type + 1;
}

pe32_u32 PE32_WINAPI pe32_user_unhook_windows_hook_ex(pe32_u32 hook)
{
    pe32_u32 slot = hook - HOOK_BASE;
    if (slot > HOOK_TYPES) {
        return 0;
    }
    hook_procs[slot] = 0;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_user_call_next_hook_ex(pe32_u32 hook, pe32_u32 code,
                                                 pe32_u32 wparam,
                                                 pe32_u32 lparam)
{
    (void)hook;
    (void)code;
    (void)wparam;
    (void)lparam;
    return 0;
}

/* Registered messages are identified by name; same name, same id. */
pe32_u32 PE32_WINAPI pe32_user_register_window_message_a(const char *name)
{
    static char names[64][64];
    static pe32_u32 count;
    pe32_u32 i, j;
    if (!name) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        for (j = 0; j < 63 && names[i][j] == name[j] && name[j]; j++) {
        }
        if (names[i][j] == name[j] || (j == 63 && !names[i][63])) {
            return MESSAGE_BASE + i;
        }
    }
    if (count == 64) {
        return 0;
    }
    for (j = 0; j < 63 && name[j]; j++) {
        names[count][j] = name[j];
    }
    names[count][j] = 0;
    next_message = MESSAGE_BASE + count + 1;
    return MESSAGE_BASE + count++;
}

/* Values of a 640x480 desktop with classic 3D metrics. */
pe32_u32 PE32_WINAPI pe32_user_get_system_metrics(pe32_u32 index)
{
    switch (index) {
    case 0: return 640;  /* SM_CXSCREEN */
    case 1: return 480;  /* SM_CYSCREEN */
    case 2: return 16;   /* SM_CXVSCROLL */
    case 3: return 16;   /* SM_CYHSCROLL */
    case 4: return 19;   /* SM_CYCAPTION */
    case 5: case 6: return 1;   /* SM_CXBORDER, SM_CYBORDER */
    case 7: case 8: return 3;   /* SM_CXDLGFRAME, SM_CYDLGFRAME */
    case 11: case 12: return 32; /* SM_CXICON, SM_CYICON */
    case 15: return 19;  /* SM_CYMENU */
    case 16: return 640; /* SM_CXFULLSCREEN */
    case 17: return 460; /* SM_CYFULLSCREEN */
    case 19: return 1;   /* SM_MOUSEPRESENT */
    case 32: case 33: return 4;  /* SM_CXFRAME, SM_CYFRAME */
    case 34: case 35: return 112; /* SM_CXMINTRACK, SM_CYMINTRACK */
    case 36: case 37: return 4;  /* SM_CXDOUBLECLK, SM_CYDOUBLECLK */
    case 43: return 3;   /* SM_CMOUSEBUTTONS */
    case 49: case 50: return 16; /* SM_CXSMICON, SM_CYSMICON */
    default: return 0;
    }
}

/* Windows 95 default scheme, 0x00bbggrr. */
pe32_u32 PE32_WINAPI pe32_user_get_sys_color(pe32_u32 index)
{
    static const pe32_u32 colors[] = {
        0x00c0c0c0, 0x00808000, 0x00800000, 0x00808080, 0x00c0c0c0,
        0x00ffffff, 0x00000000, 0x00000000, 0x00000000, 0x00ffffff,
        0x00c0c0c0, 0x00c0c0c0, 0x00808080, 0x00800000, 0x00ffffff,
        0x00c0c0c0, 0x00808080, 0x00808080, 0x00000000, 0x00c0c0c0,
        0x00ffffff, 0x00000000, 0x00c0c0c0, 0x00000000, 0x00e1ffff};
    return index < sizeof colors / sizeof colors[0] ? colors[index] : 0;
}

/* Host input: the runner forwards keyboard and mouse state from the SDL
 * front end; polling is hooked in so the guest always sees fresh state. */
static unsigned char key_down[256];
static int cursor_x = 320, cursor_y = 240;
static pe32_user_input_poll_fn input_poll;

void pe32_user_set_input_poll(pe32_user_input_poll_fn poll) { input_poll = poll; }

void pe32_user_poll_input(void)
{
    if (input_poll) {
        input_poll();
    }
}

void pe32_user_input_event(pe32_u32 type, pe32_u32 a, pe32_u32 b)
{
    if (type == PE32_INPUT_KEY && a < 256) {
        key_down[a] = b != 0;
    } else if (type == PE32_INPUT_MOVE) {
        cursor_x = (int)a;
        cursor_y = (int)b;
    } else if (type == PE32_INPUT_BUTTON && a < 256) {
        key_down[a] = b != 0;           /* VK_LBUTTON, VK_RBUTTON, VK_MBUTTON */
    } else if (type >= PE32_WINMM_AXIS && type <= PE32_WINMM_PRESENT) {
        pe32_winmm_input_event(type, a, b);
    }
}

pe32_u32 PE32_WINAPI pe32_user_get_cursor_pos(pe32_u32 *point)
{
    pe32_user_poll_input();
    if (point) {
        point[0] = (pe32_u32)cursor_x;
        point[1] = (pe32_u32)cursor_y;
    }
    return 1;
}

pe32_u32 PE32_WINAPI pe32_user_set_cursor_pos(pe32_u32 x, pe32_u32 y)
{
    cursor_x = (int)x;
    cursor_y = (int)y;
    return 1;
}

pe32_u32 PE32_WINAPI pe32_user_get_key_state(pe32_u32 vk)
{
    pe32_user_poll_input();
    return vk < 256 && key_down[vk] ? 0x8000u : 0;
}

pe32_u32 PE32_WINAPI pe32_user_get_dc(pe32_u32 window)
{
    (void)window;
    return 0x00d10001;
}

pe32_u32 PE32_WINAPI pe32_user_release_dc(pe32_u32 window, pe32_u32 dc)
{
    (void)window;
    (void)dc;
    return 1;
}

/* LoadCursor/LoadIcon/LoadBitmap/LoadMenu/...: opaque non-zero handles. */
pe32_u32 PE32_WINAPI pe32_user_load_handle(pe32_u32 module, pe32_u32 name)
{
    (void)module;
    (void)name;
    return next_handle += 4;
}

/* String tables: 16 length-prefixed UTF-16 strings per RT_STRING block. */
pe32_u32 PE32_WINAPI pe32_user_load_string_a(pe32_u32 module, pe32_u32 id,
                                             char *buffer, pe32_u32 max)
{
    pe32_u32 res, data, size, i, len;
    const unsigned char *p;
    if (!buffer || !max) {
        return 0;
    }
    res = pe32_win32_find_resource_a(module, (const char *)(unsigned long)((id >> 4) + 1),
                                     (const char *)(unsigned long)6);
    if (!res || !(data = pe32_win32_load_resource(module, res))) {
        buffer[0] = 0;
        return 0;
    }
    size = pe32_win32_sizeof_resource(module, res);
    p = (const unsigned char *)(unsigned long)data;
    for (i = 0; i < (id & 15); i++) {
        len = p[0] | (pe32_u32)p[1] << 8;
        if (size < 2 + len * 2) {
            buffer[0] = 0;
            return 0;
        }
        size -= 2 + len * 2;
        p += 2 + len * 2;
    }
    len = p[0] | (pe32_u32)p[1] << 8;
    if (size < 2 + len * 2) {
        len = 0;
    }
    if (len > max - 1) {
        len = max - 1;
    }
    for (i = 0; i < len; i++) {
        buffer[i] = (char)p[2 + i * 2]; /* Latin-1 subset is enough here */
    }
    buffer[len] = 0;
    return len;
}

pe32_u32 PE32_WINAPI pe32_user_set_message_queue(pe32_u32 size)
{
    (void)size;
    return 1;
}


/* ---- wsprintf ----------------------------------------------------------- */

/* Win32 wsprintf subset: flags - 0, width, precision, l/h, d i u x X c s %.
 * Output is capped at 1024 bytes like the real API. Returns characters written. */
static int format_va(char *out, const char *fmt, const pe32_u32 *args)
{
    char *o = out;
    while (*fmt && o - out < 1023) {
        int left = 0, zero = 0, width = 0, prec = -1, len, i;
        char tmp[16], conv;
        const char *text;
        pe32_u32 v;
        if (*fmt != '%') {
            *o++ = *fmt++;
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt != '+' && *fmt != ' ' && *fmt != '#') break;
        }
        if (*fmt == '*') { width = (int)*args++; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = (int)*args++; fmt++; }
            while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0';
        }
        while (*fmt == 'l' || *fmt == 'h') fmt++;
        conv = *fmt ? *fmt++ : 0;
        text = tmp;
        len = 0;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' ||
            conv == 'X') {
            int neg = 0;
            pe32_u32 base = (conv == 'x' || conv == 'X') ? 16 : 10;
            char rev[16];
            int n = 0;
            v = *args++;
            if ((conv == 'd' || conv == 'i') && (int)v < 0) {
                neg = 1;
                v = 0 - v;
            }
            do {
                pe32_u32 d = v % base;
                rev[n++] = (char)(d < 10 ? '0' + d
                                         : (conv == 'X' ? 'A' : 'a') + d - 10);
                v /= base;
            } while (v && n < 15);
            if (neg) tmp[len++] = '-';
            while (n) tmp[len++] = rev[--n];
        } else if (conv == 'c') {
            tmp[len++] = (char)*args++;
        } else if (conv == 's') {
            text = (const char *)(unsigned long)*args++;
            if (!text) text = "(null)";
            while (text[len] && (prec < 0 || len < prec)) len++;
        } else if (conv == '%') {
            tmp[len++] = '%';
        } else {
            continue;
        }
        for (i = len; !left && i < width && o - out < 1023; i++) {
            *o++ = (zero && conv != 's' && conv != 'c') ? '0' : ' ';
        }
        for (i = 0; i < len && o - out < 1023; i++) *o++ = text[i];
        for (i = len; left && i < width && o - out < 1023; i++) *o++ = ' ';
    }
    *o = 0;
    return (int)(o - out);
}

int pe32_user_wsprintf_a(char *out, const char *fmt, ...)
{
    return format_va(out, fmt, (const pe32_u32 *)(&fmt + 1));
}

pe32_u32 PE32_WINAPI pe32_user_wvsprintf_a(char *out, const char *fmt,
                                           const pe32_u32 *args)
{
    return (pe32_u32)format_va(out, fmt, args);
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

enum pe32_link_status pe32_user_resolve(const char *name, pe32_u32 *address)
{
#if defined(__i386__)
#define EXPOSE_USER(legacy, impl) \
    if (names_equal(name, legacy)) { \
        *address = (pe32_u32)(unsigned long)&impl; \
        return PE32_LINK_OK; \
    }
    EXPOSE_USER("SetWindowsHookExA", pe32_user_set_windows_hook_ex)
    EXPOSE_USER("UnhookWindowsHookEx", pe32_user_unhook_windows_hook_ex)
    EXPOSE_USER("CallNextHookEx", pe32_user_call_next_hook_ex)
    EXPOSE_USER("RegisterWindowMessageA", pe32_user_register_window_message_a)
    EXPOSE_USER("GetSystemMetrics", pe32_user_get_system_metrics)
    EXPOSE_USER("GetSysColor", pe32_user_get_sys_color)
    EXPOSE_USER("GetCursorPos", pe32_user_get_cursor_pos)
    EXPOSE_USER("SetCursorPos", pe32_user_set_cursor_pos)
    EXPOSE_USER("GetAsyncKeyState", pe32_user_get_key_state)
    EXPOSE_USER("GetKeyState", pe32_user_get_key_state)
    EXPOSE_USER("GetDC", pe32_user_get_dc)
    EXPOSE_USER("ReleaseDC", pe32_user_release_dc)
    EXPOSE_USER("LoadCursorA", pe32_user_load_handle)
    EXPOSE_USER("LoadIconA", pe32_user_load_handle)
    EXPOSE_USER("LoadBitmapA", pe32_user_load_handle)
    EXPOSE_USER("LoadStringA", pe32_user_load_string_a)
    EXPOSE_USER("wsprintfA", pe32_user_wsprintf_a)
    EXPOSE_USER("wvsprintfA", pe32_user_wvsprintf_a)
    EXPOSE_USER("SetMessageQueue", pe32_user_set_message_queue)
#undef EXPOSE_USER
#else
    (void)name;
    (void)address;
#endif
    return PE32_LINK_MISSING;
}
