/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_SYS_H
#define A10_PE32_SYS_H

#include "pe32_win32.h"

/* ADVAPI32 (registry), COMDLG32 and SHELL32 subsets. The registry is an
 * in-memory, session-only store seeded with the game's sound-event scheme
 * (HKCU\AppEvents\Schemes\Apps\A10Cuba); unknown keys report "not found" so
 * the game uses defaults. File dialogs
 * report "cancelled" and drag-and-drop reports no files. */
pe32_u32 PE32_WINAPI pe32_win32_reg_open_key_ex_a(pe32_u32 key, const char *sub,
                                                  pe32_u32 options,
                                                  pe32_u32 access,
                                                  pe32_u32 *result);
pe32_u32 PE32_WINAPI pe32_win32_reg_query_value_ex_a(
    pe32_u32 key, const char *name, pe32_u32 *reserved, pe32_u32 *type,
    pe32_u8 *data, pe32_u32 *size);
pe32_u32 PE32_WINAPI pe32_win32_reg_create_key_ex_a(
    pe32_u32 key, const char *sub, pe32_u32 reserved, char *cls,
    pe32_u32 options, pe32_u32 access, void *security, pe32_u32 *result,
    pe32_u32 *disposition);
pe32_u32 PE32_WINAPI pe32_win32_reg_create_key_a(pe32_u32 key, const char *sub,
                                                 pe32_u32 *result);
pe32_u32 PE32_WINAPI pe32_win32_reg_set_value_ex_a(
    pe32_u32 key, const char *name, pe32_u32 reserved, pe32_u32 type,
    const pe32_u8 *data, pe32_u32 size);
pe32_u32 PE32_WINAPI pe32_win32_reg_close_key(pe32_u32 key);

pe32_u32 PE32_WINAPI pe32_win32_get_open_file_name_a(void *ofn);
pe32_u32 PE32_WINAPI pe32_win32_get_file_title_a(const char *path, char *title,
                                                 pe32_u16 size);
pe32_u32 PE32_WINAPI pe32_win32_drag_query_file_a(pe32_u32 drop, pe32_u32 index,
                                                  char *buffer, pe32_u32 size);
void PE32_WINAPI pe32_win32_drag_finish(pe32_u32 drop);

enum pe32_link_status pe32_sys_resolve(const char *dll, const char *name,
                                       pe32_u32 *address);

#endif
