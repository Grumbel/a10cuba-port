/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_UI_LAYOUT_H
#define A10_UI_LAYOUT_H

/* Layout and selection logic of the native mission screens, in the 640x480
 * pixel space of the original backgrounds. No SDL here so it can be tested. */

struct ui_rect {
    int x, y, w, h;
};

enum ui_screen {
    UI_SCREEN_MISSIONS = 1,
    UI_SCREEN_WEAPONS = 2
};

enum ui_mission_button {
    UI_BTN_NONE = -1,
    UI_BTN_WEAPONS = 0,
    UI_BTN_MAP,
    UI_BTN_CANCEL,
    UI_BTN_OK,
    UI_BTN_COUNT
};

#define UI_MISSION_COUNT 16     /* 4 training missions and 12 campaign missions */
#define UI_TRAINING_COUNT 4
#define UI_LIST_ROW_W 271
#define UI_LIST_ROW_H 17
#define UI_LIST_ROWS 8

extern const struct ui_rect ui_mission_list, ui_mission_briefing,
    ui_mission_preview, ui_mission_buttons[UI_BTN_COUNT], ui_map_view,
    ui_map_ok;

struct ui_mission_state {
    int selected;       /* 0..UI_MISSION_COUNT-1 */
    int top;            /* first visible row */
};

int ui_point_in(const struct ui_rect *r, int x, int y);
/* Button under the point, or UI_BTN_NONE. */
int ui_mission_button_at(int x, int y);
/* List row under the point (taking the scroll position into account), or -1. */
int ui_mission_row_at(const struct ui_mission_state *s, int x, int y);
/* Select a row (clamped) and scroll it into view. */
void ui_mission_select(struct ui_mission_state *s, int index);
void ui_mission_move(struct ui_mission_state *s, int delta);


/* ---- weapons loadout ---------------------------------------------------- */

#define UI_STATIONS 11
#define UI_WEAPON_IDS 18        /* global weapon ids 0 (empty) .. 17 */
#define UI_WEAPON_W 229         /* description picture / strip entry sizes */
#define UI_WEAPON_ENTRY_W 212
#define UI_WEAPON_ENTRY_H 15
#define UI_STATION_ROW_H 26

enum ui_weapons_button {
    UI_WBTN_NONE = -1,
    UI_WBTN_DEFAULT = 0,
    UI_WBTN_UNLOAD,
    UI_WBTN_CANCEL,
    UI_WBTN_OK,
    UI_WBTN_COUNT
};

extern const struct ui_rect ui_weapons_stations, ui_weapons_desc,
    ui_weapons_buttons[UI_WBTN_COUNT], ui_weapons_fuel;

/* Pylon type 1..6 of a station (0..10): 1 2 3 4 5 6 5 4 3 2 1. */
int ui_station_type(int station);
/* Number of selectable weapons for a pylon type, and the global weapon id of
 * entry index (entry 0 is always "empty"). */
int ui_type_count(int type);
int ui_type_weapon(int type, int index);
/* Entry index of a weapon id on a pylon type, or 0 if not available. */
int ui_type_index(int type, int weapon);
/* Step the selection of a station by delta (wraps around). */
int ui_station_step(int station, int index, int delta);
/* Station row under the point, or -1. */
int ui_station_at(int x, int y);
int ui_weapons_button_at(int x, int y);
/* Parse a LoadOuts .lod header: 11 weapon ids, then a little-endian fuel
 * value in pounds. Returns 0 on success. Station selections come back as
 * entry indexes. */
int ui_lod_parse(const unsigned char *data, int size, int sel[UI_STATIONS],
                 int *fuel);

#endif
