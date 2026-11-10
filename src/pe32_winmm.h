/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_PE32_WINMM_H
#define A10_PE32_WINMM_H

#include "pe32_win32.h"

/* Millisecond monotonic clock supplied by the host. */
typedef pe32_u32 (*pe32_winmm_clock_fn)(void);
void pe32_winmm_set_clock(pe32_winmm_clock_fn clock);

/* WINMM subset imported by the EXE. Joysticks report "unplugged" until the
 * SDL3 input bridge exists; PlaySound reports success without audio. */
pe32_u32 PE32_WINAPI pe32_win32_time_get_time(void);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos(pe32_u32 id, void *info);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos_ex(pe32_u32 id, void *info);
pe32_u32 PE32_WINAPI pe32_win32_joy_get_dev_caps_a(pe32_u32 id, void *caps,
                                                   pe32_u32 size);
pe32_u32 PE32_WINAPI pe32_win32_play_sound_a(const char *sound, pe32_u32 module,
                                             pe32_u32 flags);

enum pe32_link_status pe32_winmm_resolve(const char *name, pe32_u32 *address);

#endif
