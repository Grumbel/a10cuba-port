/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>

#include "pe32_winmm.h"

static pe32_u32 fake_clock(void) { return 1234; }

int main(void)
{
    pe32_u32 address = 1;
    assert(pe32_win32_time_get_time() == 0);
    pe32_winmm_set_clock(fake_clock);
    assert(pe32_win32_time_get_time() == 1234);
    assert(pe32_win32_joy_get_pos(0, 0) == 167);
    assert(pe32_win32_joy_get_pos_ex(0, 0) == 167);
    assert(pe32_win32_joy_get_dev_caps_a(0, 0, 0) == 167);
    assert(pe32_win32_play_sound_a(0, 0, 0));
    assert(pe32_winmm_resolve("mciSendCommandA", &address) == PE32_LINK_MISSING);
    return 0;
}
