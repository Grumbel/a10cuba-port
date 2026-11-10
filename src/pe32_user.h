/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_USER_H
#define A10_PE32_USER_H

#include "pe32_win32.h"

/* Headless USER32 subset: just enough state (hooks, window messages,
 * metrics, system colors) for the statically linked MFC startup. */
pe32_u32 PE32_WINAPI pe32_user_set_windows_hook_ex(pe32_u32 type, pe32_u32 proc,
                                                   pe32_u32 module,
                                                   pe32_u32 thread);
pe32_u32 PE32_WINAPI pe32_user_unhook_windows_hook_ex(pe32_u32 hook);
pe32_u32 PE32_WINAPI pe32_user_call_next_hook_ex(pe32_u32 hook, pe32_u32 code,
                                                 pe32_u32 wparam,
                                                 pe32_u32 lparam);
pe32_u32 PE32_WINAPI pe32_user_register_window_message_a(const char *name);
pe32_u32 PE32_WINAPI pe32_user_get_system_metrics(pe32_u32 index);
pe32_u32 PE32_WINAPI pe32_user_get_sys_color(pe32_u32 index);
pe32_u32 PE32_WINAPI pe32_user_get_cursor_pos(pe32_u32 *point);
pe32_u32 PE32_WINAPI pe32_user_get_dc(pe32_u32 window);
pe32_u32 PE32_WINAPI pe32_user_release_dc(pe32_u32 window, pe32_u32 dc);
pe32_u32 PE32_WINAPI pe32_user_load_handle(pe32_u32 module, pe32_u32 name);
pe32_u32 PE32_WINAPI pe32_user_load_string_a(pe32_u32 module, pe32_u32 id,
                                             char *buffer, pe32_u32 max);
pe32_u32 PE32_WINAPI pe32_user_set_message_queue(pe32_u32 size);

/* Hook procedure registered for a type, or 0 (used by window creation). */
pe32_u32 pe32_user_hook_proc(pe32_u32 type);

enum pe32_link_status pe32_user_resolve(const char *name, pe32_u32 *address);

#endif
