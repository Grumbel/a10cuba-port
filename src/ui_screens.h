/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef A10_UI_SCREENS_H
#define A10_UI_SCREENS_H

#include <SDL3/SDL.h>

/* Native replacements for the game's dialogs, drawn from the original
 * bitmaps extracted by tools/extract_ui_assets.py into ui_dir/bmp. */

/* Run the mission list. Returns 1 when finished: *value is the chosen list
 * index (0-based) or -1 for cancel. Returns 0 if the window was closed and
 * -1 if the assets are missing. */
/* Test aid: when set, the next screen saves its first frame there as a BMP
 * and returns immediately with the initial selection. */
extern const char *ui_snapshot_path;

int ui_mission_screen(SDL_Renderer *renderer, const char *ui_dir, int initial,
                      int *value);

/* Read the default loadout of a mission list entry from the game's
 * LoadOuts directory. Returns 0 on success; on failure sel is all empty. */
int ui_weapons_default(const char *game_dir, int mission, int sel[11], int *fuel);

/* Weapons loadout: sel[11] are entry indexes per station, edited in place.
 * Returns 1 when finished (*accepted = 1 for OK, 0 for cancel), 0 if the
 * window was closed, -1 if the assets are missing. */
int ui_weapons_screen(SDL_Renderer *renderer, const char *ui_dir,
                      const char *game_dir, int mission, int sel[11],
                      int *fuel, int *accepted);

/* In-flight map opened by Esc. Returns 1 when finished; *value is
 * UI_PAUSE_RESUME, UI_PAUSE_RELOAD or UI_PAUSE_END (confirmed natively).
 * 0 if the window was closed, -1 if the assets are missing. */
int ui_pause_map_screen(SDL_Renderer *renderer, const char *ui_dir, int mission,
                        int *value);

#endif
