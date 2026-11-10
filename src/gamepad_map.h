/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_GAMEPAD_MAP_H
#define A10_GAMEPAD_MAP_H

/* Gamepad-to-keyboard layers. LB and RB act as shift keys; each layer maps
 * a button to a Windows virtual-key code (0 = unmapped). Button numbering is
 * independent of SDL so the table can be tested without it. */
enum gamepad_button {
    GP_A, GP_B, GP_X, GP_Y, GP_BACK, GP_START, GP_LSTICK, GP_RSTICK,
    GP_LB, GP_RB, GP_DUP, GP_DDOWN, GP_DLEFT, GP_DRIGHT, GP_LT, GP_RT,
    GP_BUTTON_COUNT
};

/* Layer 0 = base, 1 = LB held, 2 = RB held, 3 = LB and RB held. */
static inline unsigned gamepad_vk(int button, int lb, int rb)
{
    static const unsigned char table[4][GP_BUTTON_COUNT] = {
        /* base: A Return, B Space, X G, Y M, D-pad look, RT cannon, LT Tab */
        { 0x0d, 0x20, 'G', 'M', 0x08, 0x1b, 0, 0, 0, 0,
          0x26, 0x28, 0x25, 0x27, 0x09, 0x60 },
        /* LB: A F, B D, X C, Y A, D-pad views 1/2/3/6, LT flare, RT chaff, Back J */
        { 'F', 'D', 'C', 'A', 'J', 0x1b, 0, 0, 0, 0,
          '1', '2', '3', '6', 0xde, 0xba },
        /* RB: A S, B T, X H, Y B, D-pad F12/L/9/0, sticks Z/X, RT ECM */
        { 'S', 'T', 'H', 'B', 0x08, 0x1b, 'Z', 'X', 0, 0,
          0x7b, 'L', '9', '0', 0x09, 'E' },
        /* LB+RB: A-Y F1-F4, D-pad F5-F8 */
        { 0x70, 0x71, 0x72, 0x73, 0x08, 0x1b, 0, 0, 0, 0,
          0x74, 0x75, 0x76, 0x77, 0, 0 },
    };
    if (button < 0 || button >= GP_BUTTON_COUNT) {
        return 0;
    }
    return table[(lb ? 1 : 0) | (rb ? 2 : 0)][button];
}

#endif
