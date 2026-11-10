/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ui_layout.h"

#include <stdio.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

int main(void)
{
    struct ui_mission_state s = {0, 0};
    CHECK(ui_mission_button_at(340, 410) == UI_BTN_WEAPONS);
    CHECK(ui_mission_button_at(600, 460) == UI_BTN_OK);
    CHECK(ui_mission_button_at(10, 10) == UI_BTN_NONE);
    CHECK(ui_mission_row_at(&s, 30, 91) == 0);
    CHECK(ui_mission_row_at(&s, 30, 90 + 17 * 3) == 3);
    CHECK(ui_mission_row_at(&s, 30, 90 + 17 * 8) == -1);   /* below the list */
    ui_mission_move(&s, 9);
    CHECK(s.selected == 9 && s.top == 2);                   /* scrolled */
    CHECK(ui_mission_row_at(&s, 30, 91) == 2);
    ui_mission_move(&s, 100);
    CHECK(s.selected == UI_MISSION_COUNT - 1 && s.top == UI_MISSION_COUNT - UI_LIST_ROWS);
    ui_mission_move(&s, -100);
    CHECK(s.selected == 0 && s.top == 0);
    return 0;
}
