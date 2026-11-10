/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_sys.h"

#include "pe32_sound_events.h"
#include "pe32_winmm.h"

enum {
    ERROR_SUCCESS = 0,
    ERROR_FILE_NOT_FOUND = 2,
    ERROR_MORE_DATA = 234,
    REG_CREATED_NEW_KEY = 1,
    REG_OPENED_EXISTING_KEY = 2,
    KEY_HANDLE_BASE = 0x5000,
    MAX_KEYS = 160,
    MAX_VALUES = 256,
    PATH_MAX_LEN = 200,
    NAME_MAX_LEN = 48,
    DATA_MAX_LEN = 128
};

struct reg_key {
    char path[PATH_MAX_LEN];
};

struct reg_value {
    int key;
    char name[NAME_MAX_LEN];
    pe32_u32 type;
    pe32_u32 size;
    pe32_u8 data[DATA_MAX_LEN];
};

static struct reg_key keys[MAX_KEYS];
static int key_count;
static struct reg_value values[MAX_VALUES];
static int value_count;
static int seeded;

static unsigned lower(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int str_ieq(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return !*a && !*b;
}

static int str_len(const char *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* Append sub to path ("\" separated), trimming stray separators. */
static int build_path(char *out, const char *base, const char *sub)
{
    int n = 0, i;
    for (i = 0; base[i] && n < PATH_MAX_LEN - 1; i++) {
        out[n++] = base[i];
    }
    if (sub && *sub) {
        if (n && out[n - 1] != '\\' && n < PATH_MAX_LEN - 1) {
            out[n++] = '\\';
        }
        for (i = 0; sub[i] && n < PATH_MAX_LEN - 1; i++) {
            out[n++] = sub[i];
        }
    }
    while (n && out[n - 1] == '\\') {
        n--;
    }
    out[n] = 0;
    return n;
}

static const char *root_name(pe32_u32 key)
{
    switch (key) {
    case 0x80000000u: return "HKCR";
    case 0x80000001u: return "HKCU";
    case 0x80000002u: return "HKLM";
    case 0x80000003u: return "HKU";
    case 0x80000005u: return "HKCC";
    default: return 0;
    }
}

static int find_key(const char *path)
{
    int i;
    for (i = 0; i < key_count; i++) {
        if (str_ieq(keys[i].path, path)) {
            return i;
        }
    }
    return -1;
}

static int add_key(const char *path)
{
    int i = find_key(path);
    if (i >= 0) {
        return i;
    }
    if (key_count >= MAX_KEYS) {
        return -1;
    }
    build_path(keys[key_count].path, path, 0);
    return key_count++;
}

static int set_value(int key, const char *name, pe32_u32 type,
                     const pe32_u8 *data, pe32_u32 size)
{
    int i;
    pe32_u32 j;
    if (size > DATA_MAX_LEN || str_len(name) >= NAME_MAX_LEN) {
        return 0;
    }
    for (i = 0; i < value_count; i++) {
        if (values[i].key == key && str_ieq(values[i].name, name)) {
            break;
        }
    }
    if (i == value_count) {
        if (value_count >= MAX_VALUES) {
            return 0;
        }
        value_count++;
        values[i].key = key;
        for (j = 0; (values[i].name[j] = name[j]) != 0; j++) {
        }
    }
    values[i].type = type;
    values[i].size = size;
    for (j = 0; j < size; j++) {
        values[i].data[j] = data[j];
    }
    return 1;
}

/* Seed HKCU\AppEvents\Schemes\Apps\A10Cuba\<event>\.Current (REG_SZ) so the
 * game finds its sound files without a Windows registry. */
static void seed(void)
{
    unsigned i;
    char path[PATH_MAX_LEN];
    if (seeded) {
        return;
    }
    seeded = 1;
    for (i = 0; i < sizeof(sound_events) / sizeof(sound_events[0]); i++) {
        int k;
        build_path(path, "HKCU\\AppEvents\\Schemes\\Apps\\A10Cuba",
                   sound_events[i][0]);
        k = add_key(path);
        if (k >= 0) {
            build_path(path, keys[k].path, ".Current");
            k = add_key(path);
        }
        if (k >= 0) {
            set_value(k, "", 1, (const pe32_u8 *)sound_events[i][1],
                      (pe32_u32)str_len(sound_events[i][1]) + 1);
        }
    }
    /* The game flies with the mouse by default; Device=3 selects the
     * joystick (found empirically). Select it up front when a gamepad is
     * present; the Joystick value is the joystick id. */
    if (pe32_winmm_joystick_present()) {
        int k;
        add_key("HKCU\\Software");
        add_key("HKCU\\Software\\Parsoft");
        add_key("HKCU\\Software\\Parsoft\\A-10 Cuba");
        add_key("HKCU\\Software\\Parsoft\\A-10 Cuba\\Preferences");
        k = add_key("HKCU\\Software\\Parsoft\\A-10 Cuba\\Preferences\\Devices");
        if (k >= 0) {
            static const pe32_u8 three[4] = {3, 0, 0, 0}, zero[4] = {0, 0, 0, 0};
            set_value(k, "Device", 4, three, 4);
            set_value(k, "Joystick", 4, zero, 4);
        }
    }
}

/* Full path of an open handle or predefined root, or -1. */
static int handle_path(pe32_u32 key, char *out)
{
    const char *root = root_name(key);
    if (root) {
        build_path(out, root, 0);
        return 1;
    }
    if (key >= KEY_HANDLE_BASE && key < KEY_HANDLE_BASE + MAX_KEYS &&
        (int)(key - KEY_HANDLE_BASE) < key_count) {
        build_path(out, keys[key - KEY_HANDLE_BASE].path, 0);
        return 1;
    }
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_open_key_ex_a(pe32_u32 key, const char *sub,
                                                  pe32_u32 options,
                                                  pe32_u32 access,
                                                  pe32_u32 *result)
{
    char base[PATH_MAX_LEN], path[PATH_MAX_LEN];
    int k;
    (void)options; (void)access;
    seed();
    if (result) {
        *result = 0;
    }
    if (!handle_path(key, base)) {
        return ERROR_FILE_NOT_FOUND;
    }
    build_path(path, base, sub);
    k = find_key(path);
    if (k < 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (result) {
        *result = KEY_HANDLE_BASE + (pe32_u32)k;
    }
    return ERROR_SUCCESS;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_query_value_ex_a(
    pe32_u32 key, const char *name, pe32_u32 *reserved, pe32_u32 *type,
    pe32_u8 *data, pe32_u32 *size)
{
    char base[PATH_MAX_LEN];
    int k, i;
    (void)reserved;
    seed();
    if (!handle_path(key, base) || (k = find_key(base)) < 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    for (i = 0; i < value_count; i++) {
        if (values[i].key == k && str_ieq(values[i].name, name ? name : "")) {
            pe32_u32 j;
            if (type) {
                *type = values[i].type;
            }
            if (data && size) {
                if (*size < values[i].size) {
                    *size = values[i].size;
                    return ERROR_MORE_DATA;
                }
                for (j = 0; j < values[i].size; j++) {
                    data[j] = values[i].data[j];
                }
            }
            if (size) {
                *size = values[i].size;
            }
            return ERROR_SUCCESS;
        }
    }
    return ERROR_FILE_NOT_FOUND;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_create_key_ex_a(
    pe32_u32 key, const char *sub, pe32_u32 reserved, char *cls,
    pe32_u32 options, pe32_u32 access, void *security, pe32_u32 *result,
    pe32_u32 *disposition)
{
    char base[PATH_MAX_LEN], path[PATH_MAX_LEN];
    int k, existed;
    (void)reserved; (void)cls; (void)options; (void)access; (void)security;
    seed();
    if (!handle_path(key, base)) {
        return ERROR_FILE_NOT_FOUND;
    }
    build_path(path, base, sub);
    existed = find_key(path) >= 0;
    k = add_key(path);
    if (k < 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (result) {
        *result = KEY_HANDLE_BASE + (pe32_u32)k;
    }
    if (disposition) {
        *disposition = existed ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
    }
    return ERROR_SUCCESS;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_create_key_a(pe32_u32 key, const char *sub,
                                                 pe32_u32 *result)
{
    return pe32_win32_reg_create_key_ex_a(key, sub, 0, 0, 0, 0, 0, result, 0);
}

pe32_u32 PE32_WINAPI pe32_win32_reg_set_value_ex_a(
    pe32_u32 key, const char *name, pe32_u32 reserved, pe32_u32 type,
    const pe32_u8 *data, pe32_u32 size)
{
    char base[PATH_MAX_LEN];
    int k;
    (void)reserved;
    seed();
    if (!handle_path(key, base) || (k = find_key(base)) < 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    /* Oversized values are dropped; the game then falls back to defaults. */
    set_value(k, name ? name : "", type, data, size);
    return ERROR_SUCCESS;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_close_key(pe32_u32 key)
{
    (void)key;
    return ERROR_SUCCESS;
}

pe32_u32 PE32_WINAPI pe32_win32_get_open_file_name_a(void *ofn)
{
    (void)ofn;
    return 0; /* Cancelled (CommDlgExtendedError would be 0). */
}

pe32_u32 PE32_WINAPI pe32_win32_get_file_title_a(const char *path, char *title,
                                                 pe32_u16 size)
{
    pe32_u32 start = 0, i = 0, length;
    if (!path || !title) {
        return (pe32_u32)-1;
    }
    while (path[i]) {
        if (path[i] == '\\' || path[i] == '/' || path[i] == ':') {
            start = i + 1;
        }
        i++;
    }
    length = i - start;
    if (length + 1 > size) {
        return length + 1; /* Required buffer size, as documented. */
    }
    for (i = 0; i <= length; i++) {
        title[i] = path[start + i];
    }
    return 0;
}

pe32_u32 PE32_WINAPI pe32_win32_drag_query_file_a(pe32_u32 drop, pe32_u32 index,
                                                  char *buffer, pe32_u32 size)
{
    (void)drop; (void)index; (void)buffer; (void)size;
    return 0;
}

void PE32_WINAPI pe32_win32_drag_finish(pe32_u32 drop) { (void)drop; }

#if defined(__i386__)
static int names_equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int dll_is(const char *a, const char *b)
{
    while (*a && ((*a | 32) == (*b | 32))) {
        a++;
        b++;
    }
    return !*a && !*b;
}
#endif

enum pe32_link_status pe32_sys_resolve(const char *dll, const char *name,
                                       pe32_u32 *address)
{
#if defined(__i386__)
#define EXPOSE_SYS(library, legacy, impl) \
    if (dll_is(dll, library) && names_equal(name, legacy)) { \
        *address = (pe32_u32)(unsigned long)&impl; \
        return PE32_LINK_OK; \
    }
    EXPOSE_SYS("ADVAPI32.dll", "RegOpenKeyExA", pe32_win32_reg_open_key_ex_a)
    EXPOSE_SYS("ADVAPI32.dll", "RegQueryValueExA", pe32_win32_reg_query_value_ex_a)
    EXPOSE_SYS("ADVAPI32.dll", "RegCreateKeyExA", pe32_win32_reg_create_key_ex_a)
    EXPOSE_SYS("ADVAPI32.dll", "RegCreateKeyA", pe32_win32_reg_create_key_a)
    EXPOSE_SYS("ADVAPI32.dll", "RegSetValueExA", pe32_win32_reg_set_value_ex_a)
    EXPOSE_SYS("ADVAPI32.dll", "RegCloseKey", pe32_win32_reg_close_key)
    EXPOSE_SYS("comdlg32.dll", "GetOpenFileNameA", pe32_win32_get_open_file_name_a)
    EXPOSE_SYS("comdlg32.dll", "GetSaveFileNameA", pe32_win32_get_open_file_name_a)
    EXPOSE_SYS("comdlg32.dll", "GetFileTitleA", pe32_win32_get_file_title_a)
    EXPOSE_SYS("SHELL32.dll", "DragQueryFileA", pe32_win32_drag_query_file_a)
    EXPOSE_SYS("SHELL32.dll", "DragFinish", pe32_win32_drag_finish)
#undef EXPOSE_SYS
#else
    (void)dll;
    (void)name;
    (void)address;
#endif
    return PE32_LINK_MISSING;
}
