/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <assert.h>

#include "gamepad_map.h"

int main(void)
{
    int layer, button;
    assert(gamepad_vk(GP_A, 0, 0) == 0x0d);
    assert(gamepad_vk(GP_RT, 0, 0) == 0x60);
    assert(gamepad_vk(GP_RT, 1, 0) == 0xba);
    assert(gamepad_vk(GP_LT, 1, 0) == 0xde);
    assert(gamepad_vk(GP_RT, 0, 1) == 'E');
    assert(gamepad_vk(GP_A, 1, 1) == 0x70);
    assert(gamepad_vk(GP_DRIGHT, 1, 1) == 0x77);
    assert(gamepad_vk(GP_START, 0, 0) == 0x1b);
    assert(gamepad_vk(GP_BUTTON_COUNT, 0, 0) == 0);
    /* The shift buttons themselves never produce keys. */
    for (layer = 0; layer < 4; layer++) {
        assert(gamepad_vk(GP_LB, layer & 1, layer >> 1) == 0);
        assert(gamepad_vk(GP_RB, layer & 1, layer >> 1) == 0);
    }
    /* No two buttons in one layer share a key. */
    for (layer = 0; layer < 4; layer++) {
        for (button = 0; button < GP_BUTTON_COUNT; button++) {
            int other;
            unsigned vk = gamepad_vk(button, layer & 1, layer >> 1);
            for (other = button + 1; vk && other < GP_BUTTON_COUNT; other++) {
                assert(gamepad_vk(other, layer & 1, layer >> 1) != vk);
            }
        }
    }
    return 0;
}
