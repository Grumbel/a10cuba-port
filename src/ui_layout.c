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
