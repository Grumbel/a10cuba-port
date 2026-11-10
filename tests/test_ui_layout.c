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
    /* in-flight map */
    CHECK(ui_pause_button_at(40, 440) == UI_PBTN_RELOAD);
    CHECK(ui_pause_button_at(420, 440) == UI_PBTN_END);
    CHECK(ui_pause_button_at(510, 440) == UI_PBTN_RESUME);
    CHECK(ui_pause_button_at(300, 100) == UI_PBTN_NONE);
    CHECK(ui_pause_step(UI_PBTN_RESUME, 1) == UI_PBTN_RESUME);
    CHECK(ui_pause_step(UI_PBTN_RELOAD, -1) == UI_PBTN_RELOAD);
    CHECK(ui_pause_step(UI_PBTN_RELOAD, 1) == UI_PBTN_END);
    CHECK(ui_pause_command(UI_PBTN_END) == 1055);
    CHECK(ui_pause_command(UI_PBTN_RESUME) == 1);
    CHECK(ui_map_heading(100, 100, 100, 50) == 0);
    CHECK(ui_map_heading(100, 100, 150, 100) == 90);
    CHECK(ui_map_heading(100, 100, 100, 150) == 180);
    CHECK(ui_map_heading(100, 100, 50, 100) == 270);
    CHECK(ui_map_heading(100, 100, 150, 50) == 45);
    CHECK(ui_map_heading(100, 100, 50, 150) == 225);
    CHECK(ui_map_heading(5, 5, 5, 5) == -1);
    return 0;
}
