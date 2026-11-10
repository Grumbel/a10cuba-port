/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ui_layout.h"

/* Positions measured against the original background bitmaps (resource 142
 * for the mission list, 144 for the map). */
const struct ui_rect ui_mission_list = {20, 90, UI_LIST_ROW_W, UI_LIST_ROWS * UI_LIST_ROW_H};
const struct ui_rect ui_mission_briefing = {329, 90, 286, 295};
const struct ui_rect ui_mission_preview = {20, 267, 272, 187};
const struct ui_rect ui_mission_buttons[UI_BTN_COUNT] = {
    {339, 409, 131, 28},        /* weapons loadout */
    {473, 409, 131, 28},        /* mission map */
    {339, 439, 131, 27},        /* cancel */
    {473, 439, 131, 27},        /* fly mission */
};
const struct ui_rect ui_map_view = {16, 17, 608, 408};
const struct ui_rect ui_map_ok = {505, 437, 100, 30};

int ui_point_in(const struct ui_rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int ui_mission_button_at(int x, int y)
{
    int i;
    for (i = 0; i < UI_BTN_COUNT; i++) {
        if (ui_point_in(&ui_mission_buttons[i], x, y)) {
            return i;
        }
    }
    return UI_BTN_NONE;
}

int ui_mission_row_at(const struct ui_mission_state *s, int x, int y)
{
    int row;
    if (!ui_point_in(&ui_mission_list, x, y)) {
        return -1;
    }
    row = s->top + (y - ui_mission_list.y) / UI_LIST_ROW_H;
    return row < UI_MISSION_COUNT ? row : -1;
}

void ui_mission_select(struct ui_mission_state *s, int index)
{
    if (index < 0) {
        index = 0;
    }
    if (index >= UI_MISSION_COUNT) {
        index = UI_MISSION_COUNT - 1;
    }
    s->selected = index;
    if (index < s->top) {
        s->top = index;
    }
    if (index >= s->top + UI_LIST_ROWS) {
        s->top = index - UI_LIST_ROWS + 1;
    }
}

void ui_mission_move(struct ui_mission_state *s, int delta)
{
    ui_mission_select(s, s->selected + delta);
}

/* ---- weapons loadout ---------------------------------------------------- */

/* Measured against background 140. */
const struct ui_rect ui_weapons_stations = {99, 41, 215, UI_STATIONS * UI_STATION_ROW_H};
const struct ui_rect ui_weapons_desc = {387, 41, 230, 280};
const struct ui_rect ui_weapons_buttons[UI_WBTN_COUNT] = {
    {41, 399, 148, 30},         /* default loadout */
    {191, 399, 146, 30},        /* unload all */
    {401, 439, 101, 31},        /* cancel */
    {505, 439, 101, 31},        /* ok */
};
const struct ui_rect ui_weapons_fuel = {391, 381, 236, 30};

/* Global weapon ids selectable on each pylon type, in the order of the
 * original strips (WEPSTAnDS); entry 0 is empty. */
static const signed char type_1[] = {0, 1, 2, 6, 9, 10, 11, 12};
static const signed char type_2[] = {0, 6, 8, 9, 10, 11, 12, 13, 14};
static const signed char type_3[] = {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static const signed char type_4[] = {0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
static const signed char type_5[] = {0, 9, 10, 11, 12, 13, 14, 15, 16};
static const signed char type_6[] = {0};

static const signed char *const type_tab[6] = {type_1, type_2, type_3, type_4, type_5, type_6};
static const int type_len[6] = {
    (int)sizeof type_1, (int)sizeof type_2, (int)sizeof type_3,
    (int)sizeof type_4, (int)sizeof type_5, (int)sizeof type_6
};

int ui_station_type(int station)
{
    if (station < 0 || station >= UI_STATIONS) {
        return 6;
    }
    return station <= 5 ? station + 1 : UI_STATIONS - station;
}

int ui_type_count(int type)
{
    return type >= 1 && type <= 6 ? type_len[type - 1] : 1;
}

int ui_type_weapon(int type, int index)
{
    if (type < 1 || type > 6 || index < 0 || index >= type_len[type - 1]) {
        return 0;
    }
    return type_tab[type - 1][index];
}

int ui_type_index(int type, int weapon)
{
    int i;
    for (i = 0; i < ui_type_count(type); i++) {
        if (ui_type_weapon(type, i) == weapon) {
            return i;
        }
    }
    return 0;
}

int ui_station_step(int station, int index, int delta)
{
    int n = ui_type_count(ui_station_type(station));
    index = (index + delta) % n;
    return index < 0 ? index + n : index;
}

int ui_station_at(int x, int y)
{
    if (!ui_point_in(&ui_weapons_stations, x, y)) {
        return -1;
    }
    return (y - ui_weapons_stations.y) / UI_STATION_ROW_H;
}

int ui_weapons_button_at(int x, int y)
{
    int i;
    for (i = 0; i < UI_WBTN_COUNT; i++) {
        if (ui_point_in(&ui_weapons_buttons[i], x, y)) {
            return i;
        }
    }
    return UI_WBTN_NONE;
}

int ui_lod_parse(const unsigned char *data, int size, int sel[UI_STATIONS],
                 int *fuel)
{
    int i;
    if (size < UI_STATIONS + 4) {
        return -1;
    }
    for (i = 0; i < UI_STATIONS; i++) {
        sel[i] = ui_type_index(ui_station_type(i), data[i]);
    }
    *fuel = data[11] | data[12] << 8 | data[13] << 16 | data[14] << 24;
    return 0;
}
