/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_WINMM_H
#define A10_PE32_WINMM_H

#include "pe32_win32.h"

/* Millisecond monotonic clock supplied by the host. */
typedef pe32_u32 (*pe32_winmm_clock_fn)(void);
void pe32_winmm_set_clock(pe32_winmm_clock_fn clock);

/* Joystick events from the host (input record types 4-6). Joystick 0 reports
 * "unplugged" until the host announces a gamepad. */
enum { PE32_WINMM_AXIS = 4, PE32_WINMM_BUTTON = 5, PE32_WINMM_PRESENT = 6 };
void pe32_winmm_input_event(pe32_u32 type, pe32_u32 a, pe32_u32 b);
int pe32_winmm_joystick_present(void);

/* WINMM subset imported by the EXE. PlaySound reports success without audio. */
pe32_u32 PE32_WINAPI pe32_win32_time_get_time(void);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_num_devs(void);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos(pe32_u32 id, void *info);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos_ex(pe32_u32 id, void *info);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_dev_caps_a(pe32_u32 id, void *caps,
                                                   pe32_u32 size);
pe32_u32 PE32_WINAPI pe32_win32_play_sound_a(const char *sound, pe32_u32 module,
                                             pe32_u32 flags);

enum pe32_link_status pe32_winmm_resolve(const char *name, pe32_u32 *address);

#endif
