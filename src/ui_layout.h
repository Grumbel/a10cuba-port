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
    UI_SCREEN_MISSIONS = 1
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

#endif
