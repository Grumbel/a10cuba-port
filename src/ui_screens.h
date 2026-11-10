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

#endif
