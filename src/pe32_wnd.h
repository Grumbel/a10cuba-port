/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_WND_H
#define A10_PE32_WND_H

#include "pe32_win32.h"

/* Window manager for the statically linked MFC code: window classes, a
 * window table with guest window procedures, a message queue, timers and
 * dialog templates. Nothing is drawn; the game's own DirectDraw surfaces are
 * the only visible output. */

/* Host hooks: sleep (ms) and optional input injection into the queue. */
typedef void (*pe32_wnd_idle_fn)(pe32_u32 wait_ms);
void pe32_wnd_set_idle(pe32_wnd_idle_fn idle);

/* Queue a message from the host (e.g. SDL input), 0 on overflow. */
int pe32_wnd_post(pe32_u32 hwnd, pe32_u32 msg, pe32_u32 wparam,
                  pe32_u32 lparam);

/* Window that should receive keyboard/mouse input (focus, else active), 0 if
 * none exists yet. */
pe32_u32 pe32_wnd_input_target(void);
void pe32_wnd_set_game_window(pe32_u32 hwnd);
void pe32_wnd_set_mission(int index);
/* Native screen hook: screen id and argument in, result out (>= 0 a choice,
 * -1 cancelled, -2 no native screen available). */
typedef int (*pe32_wnd_ui_fn)(int screen, int arg);
void pe32_wnd_set_ui(pe32_wnd_ui_fn ui);
int pe32_wnd_listbox_count(pe32_u32 hwnd);
void pe32_wnd_listbox_select(pe32_u32 hwnd, int index);

/* When set, a blocked GetMessage with nothing pending clicks the default
 * button (IDOK) of the active dialog, so unattended runs progress. */
void pe32_wnd_set_auto_accept(int enable);

enum pe32_link_status pe32_wnd_resolve(const char *name, pe32_u32 *address);

#endif
