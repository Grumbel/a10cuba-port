/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_sys.h"

enum {
    ERROR_SUCCESS = 0,
    ERROR_FILE_NOT_FOUND = 2,
    REG_CREATED_NEW_KEY = 1,
    KEY_HANDLE = 0x5000
};

pe32_u32 PE32_WINAPI pe32_win32_reg_open_key_ex_a(pe32_u32 key, const char *sub,
                                                  pe32_u32 options,
                                                  pe32_u32 access,
                                                  pe32_u32 *result)
{
    (void)key; (void)sub; (void)options; (void)access;
    if (result) {
        *result = 0;
    }
    return ERROR_FILE_NOT_FOUND;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_query_value_ex_a(
    pe32_u32 key, const char *name, pe32_u32 *reserved, pe32_u32 *type,
    pe32_u8 *data, pe32_u32 *size)
{
    (void)key; (void)name; (void)reserved; (void)type; (void)data; (void)size;
    return ERROR_FILE_NOT_FOUND;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_create_key_ex_a(
    pe32_u32 key, const char *sub, pe32_u32 reserved, char *cls,
    pe32_u32 options, pe32_u32 access, void *security, pe32_u32 *result,
    pe32_u32 *disposition)
{
    (void)key; (void)sub; (void)reserved; (void)cls; (void)options;
    (void)access; (void)security;
    if (result) {
        *result = KEY_HANDLE;
    }
    if (disposition) {
        *disposition = REG_CREATED_NEW_KEY;
    }
    return ERROR_SUCCESS;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_create_key_a(pe32_u32 key, const char *sub,
                                                 pe32_u32 *result)
{
    (void)key; (void)sub;
    if (result) {
        *result = KEY_HANDLE;
    }
    return ERROR_SUCCESS;
}

pe32_u32 PE32_WINAPI pe32_win32_reg_set_value_ex_a(
    pe32_u32 key, const char *name, pe32_u32 reserved, pe32_u32 type,
    const pe32_u8 *data, pe32_u32 size)
{
    (void)key; (void)name; (void)reserved; (void)type; (void)data; (void)size;
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
