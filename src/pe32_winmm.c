/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_winmm.h"

enum { JOYERR_UNPLUGGED = 167 };

static pe32_winmm_clock_fn backend_clock;

void pe32_winmm_set_clock(pe32_winmm_clock_fn clock)
{
    backend_clock = clock;
}

pe32_u32 PE32_WINAPI pe32_win32_time_get_time(void)
{
    return backend_clock ? backend_clock() : 0;
}

pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos(pe32_u32 id, void *info)
{
    (void)id;
    (void)info;
    return JOYERR_UNPLUGGED;
}

pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos_ex(pe32_u32 id, void *info)
{
    (void)id;
    (void)info;
    return JOYERR_UNPLUGGED;
}

pe32_u32 PE32_WINAPI pe32_win32_joy_get_dev_caps_a(pe32_u32 id, void *caps,
                                                   pe32_u32 size)
{
    (void)id;
    (void)caps;
    (void)size;
    return JOYERR_UNPLUGGED;
}

pe32_u32 PE32_WINAPI pe32_win32_play_sound_a(const char *sound, pe32_u32 module,
                                             pe32_u32 flags)
{
    (void)sound;
    (void)module;
    (void)flags;
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

enum pe32_link_status pe32_winmm_resolve(const char *name, pe32_u32 *address)
{
#if defined(__i386__)
#define EXPOSE_WINMM(legacy, impl) \
    if (names_equal(name, legacy)) { \
        *address = (pe32_u32)(unsigned long)&impl; \
        return PE32_LINK_OK; \
    }
    EXPOSE_WINMM("timeGetTime", pe32_win32_time_get_time)
    EXPOSE_WINMM("joyGetPos", pe32_win32_joy_get_pos)
    EXPOSE_WINMM("joyGetPosEx", pe32_win32_joy_get_pos_ex)
    EXPOSE_WINMM("joyGetDevCapsA", pe32_win32_joy_get_dev_caps_a)
    EXPOSE_WINMM("PlaySoundA", pe32_win32_play_sound_a)
#undef EXPOSE_WINMM
#else
    (void)name;
    (void)address;
#endif
    return PE32_LINK_MISSING;
}
