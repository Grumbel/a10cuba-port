/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pe32_winmm.h"
#include "pe32_user.h"

enum { JOYERR_NOERROR = 0, JOYERR_PARMS = 165, JOYERR_UNPLUGGED = 167 };

static pe32_winmm_clock_fn backend_clock;

void pe32_winmm_set_clock(pe32_winmm_clock_fn clock)
{
    backend_clock = clock;
}

pe32_u32 PE32_WINAPI pe32_win32_time_get_time(void)
{
    return backend_clock ? backend_clock() : 0;
}

/* Joystick state fed by the host (first gamepad = joystick 0). Axes are
 * X, Y, Z, R, U, V in 0..65535 with 32767 as the centre. */
static int joy_present;
static pe32_u32 joy_axes[6] = {32767, 32767, 32767, 32767, 32767, 32767};
static pe32_u32 joy_buttons;

void pe32_winmm_input_event(pe32_u32 type, pe32_u32 a, pe32_u32 b)
{
    if (type == PE32_WINMM_AXIS && a < 6) {
        joy_axes[a] = b > 65535 ? 65535 : b;
    } else if (type == PE32_WINMM_BUTTON && a < 32) {
        joy_buttons = b ? joy_buttons | (1u << a) : joy_buttons & ~(1u << a);
    } else if (type == PE32_WINMM_PRESENT) {
        joy_present = a != 0;
        if (!joy_present) {
            unsigned i;
            for (i = 0; i < 6; i++) {
                joy_axes[i] = 32767;
            }
            joy_buttons = 0;
        }
    }
}

static pe32_u32 joy_check(pe32_u32 id)
{
    pe32_user_poll_input();
    return id == 0 && joy_present ? JOYERR_NOERROR : JOYERR_UNPLUGGED;
}

pe32_u32 PE32_WINAPI pe32_win32_joy_get_num_devs(void) { return 1; }

pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos(pe32_u32 id, void *info)
{
    pe32_u32 *out = info, err = joy_check(id);
    if (err || !out) {
        return err ? err : JOYERR_PARMS;
    }
    out[0] = joy_axes[0];
    out[1] = joy_axes[1];
    out[2] = joy_axes[2];
    out[3] = joy_buttons & 0xf;
    return JOYERR_NOERROR;
}

/* JOYINFOEX: size, flags, X, Y, Z, R, U, V, buttons, button number, POV. */
pe32_u32 PE32_WINAPI pe32_win32_joy_get_pos_ex(pe32_u32 id, void *info)
{
    pe32_u32 *out = info, err = joy_check(id);
    unsigned i;
    if (err || !out) {
        return err ? err : JOYERR_PARMS;
    }
    if (out[0] < 13 * 4) {
        return JOYERR_PARMS;
    }
    for (i = 0; i < 6; i++) {
        out[2 + i] = joy_axes[i];
    }
    out[8] = joy_buttons;
    out[9] = 0;
    for (i = 0; i < 32; i++) {
        out[9] += (joy_buttons >> i) & 1;
    }
    out[10] = 0xffff; /* JOY_POVCENTERED: the D-pad is mapped to keys. */
    return JOYERR_NOERROR;
}

/* JOYCAPSA: 2 WORDs, name[32], then UINTs (see mmsystem.h). */
pe32_u32 PE32_WINAPI pe32_win32_joy_get_dev_caps_a(pe32_u32 id, void *caps,
                                                   pe32_u32 size)
{
    pe32_u32 *u = caps;
    unsigned char *bytes = caps;
    static const char name[] = "SDL Gamepad";
    unsigned i;
    pe32_u32 err = joy_check(id);
    if (err || !caps) {
        return err ? err : JOYERR_PARMS;
    }
    if (size < 404) {
        return JOYERR_PARMS;
    }
    for (i = 0; i < size; i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < sizeof name; i++) {
        bytes[4 + i] = (unsigned char)name[i];
    }
    u[10] = 65535; u[12] = 65535; u[14] = 65535;   /* X, Y, Z max */
    u[15] = 32;                          /* wNumButtons */
    u[16] = 10;                          /* wPeriodMin */
    u[17] = 1000;                        /* wPeriodMax */
    u[18] = 0; u[19] = 65535;            /* R */
    u[20] = 0; u[21] = 65535;            /* U */
    u[22] = 0; u[23] = 65535;            /* V */
    u[24] = 1 | 2 | 4 | 8;               /* HASZ | HASR | HASU | HASV */
    u[25] = 6;                           /* wMaxAxes */
    u[26] = 6;                           /* wNumAxes */
    u[27] = 32;                          /* wMaxButtons */
    return JOYERR_NOERROR;
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
    EXPOSE_WINMM("joyGetNumDevs", pe32_win32_joy_get_num_devs)
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
