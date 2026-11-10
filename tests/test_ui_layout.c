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
    /* weapons loadout */
    {
        static const unsigned char lod[] = {1, 6, 3, 9, 15, 0, 15, 9, 3, 6, 1,
                                            0x46, 0x05, 0, 0, 0xff};
        int sel[UI_STATIONS], fuel, i;
        CHECK(ui_lod_parse(lod, sizeof lod, sel, &fuel) == 0);
        CHECK(fuel == 1350);
        for (i = 0; i < UI_STATIONS; i++) {
            CHECK(ui_type_weapon(ui_station_type(i), sel[i]) == lod[i]);
        }
        CHECK(ui_station_type(0) == 1 && ui_station_type(5) == 6 &&
              ui_station_type(10) == 1 && ui_station_type(7) == 4);
        CHECK(ui_type_count(3) == 16 && ui_type_count(6) == 1);
        CHECK(ui_type_index(1, 17) == 0);                  /* not available */
        CHECK(ui_station_step(0, 0, -1) == 7);             /* wraps */
        CHECK(ui_station_step(5, 0, 1) == 0);
        CHECK(ui_station_at(120, 41 + 26 * 4 + 3) == 4);
        CHECK(ui_station_at(20, 50) == -1);
        CHECK(ui_weapons_button_at(520, 450) == UI_WBTN_OK);
        CHECK(ui_lod_parse(lod, 5, sel, &fuel) != 0);
    }
    return 0;
}
