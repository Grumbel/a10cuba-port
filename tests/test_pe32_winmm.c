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
    assert(pe32_win32_joy_get_num_devs() == 1);
    {
        pe32_u32 info[4], ex[13] = {52}, caps[101] = {0};
        assert(pe32_win32_joy_get_pos(0, info) == 167);
        pe32_winmm_input_event(PE32_WINMM_PRESENT, 1, 0);
        pe32_winmm_input_event(PE32_WINMM_AXIS, 0, 65535);
        pe32_winmm_input_event(PE32_WINMM_AXIS, 3, 100);
        pe32_winmm_input_event(PE32_WINMM_BUTTON, 2, 1);
        assert(pe32_win32_joy_get_pos(0, info) == 0);
        assert(info[0] == 65535 && info[1] == 32767 && info[3] == 4);
        assert(pe32_win32_joy_get_pos(1, info) == 167);
        assert(pe32_win32_joy_get_pos_ex(0, ex) == 0);
        assert(ex[2] == 65535 && ex[5] == 100 && ex[8] == 4 && ex[9] == 1);
        assert(pe32_win32_joy_get_dev_caps_a(0, caps, 404) == 0);
        assert(caps[10] == 65535 && caps[15] == 32 && caps[27] == 32);
        pe32_winmm_input_event(PE32_WINMM_PRESENT, 0, 0);
        assert(pe32_win32_joy_get_pos(0, info) == 167);
    }
    assert(pe32_win32_play_sound_a(0, 0, 0));
    assert(pe32_winmm_resolve("mciSendCommandA", &address) == PE32_LINK_MISSING);
    return 0;
}
