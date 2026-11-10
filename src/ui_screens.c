/* SPDX-FileCopyrightText: 2026 Ingo Ruhnke */
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ui_screens.h"

#include "ui_layout.h"
#include <stdio.h>
#include <string.h>

#define MAX_TEXTURES 96

const char *ui_snapshot_path;

struct ui_assets {
    SDL_Renderer *renderer;
    const char *dir;
    char names[MAX_TEXTURES][32];
    SDL_Texture *textures[MAX_TEXTURES];
    int count;
};

static SDL_Texture *asset(struct ui_assets *a, const char *name)
{
    char path[4096];
    SDL_Surface *surface;
    int i;
    for (i = 0; i < a->count; i++) {
        if (!strcmp(a->names[i], name)) {
            return a->textures[i];
        }
    }
    if (a->count >= MAX_TEXTURES) {
        return NULL;
    }
    snprintf(path, sizeof path, "%s/bmp/%s.bmp", a->dir, name);
    surface = SDL_LoadBMP(path);
    a->textures[a->count] = NULL;
    if (surface) {
        a->textures[a->count] = SDL_CreateTextureFromSurface(a->renderer, surface);
        SDL_DestroySurface(surface);
    }
    if (a->textures[a->count]) {
        SDL_SetTextureScaleMode(a->textures[a->count], SDL_SCALEMODE_NEAREST);
    }
    snprintf(a->names[a->count], sizeof a->names[0], "%s", name);
    return a->textures[a->count++];
}

static void draw(struct ui_assets *a, const char *name, int x, int y)
{
    SDL_Texture *t = asset(a, name);
    SDL_FRect dst = {(float)x, (float)y, 0, 0};
    if (t) {
        SDL_GetTextureSize(t, &dst.w, &dst.h);
        SDL_RenderTexture(a->renderer, t, NULL, &dst);
    }
}

static void draw_row(struct ui_assets *a, const char *strip, int index, int x, int y)
{
    SDL_Texture *t = asset(a, strip);
    SDL_FRect src = {(float)(index * UI_LIST_ROW_W), 0, UI_LIST_ROW_W, UI_LIST_ROW_H};
    SDL_FRect dst = {(float)x, (float)y, UI_LIST_ROW_W, UI_LIST_ROW_H};
    if (t) {
        SDL_RenderTexture(a->renderer, t, &src, &dst);
    }
}

static void free_assets(struct ui_assets *a)
{
    int i;
    for (i = 0; i < a->count; i++) {
        if (a->textures[i]) {
            SDL_DestroyTexture(a->textures[i]);
        }
    }
}

/* Button name stems per mission-list button (bitmap NAME + U/F/D). */
static const char *const button_stem[UI_BTN_COUNT] = {
    "MISWEAPONS", "MISMAP", "MISCANCEL", "MISOK"
};

static void draw_button(struct ui_assets *a, const char *stem, char state,
                        const struct ui_rect *r)
{
    char name[40];
    snprintf(name, sizeof name, "%s%c", stem, state);
    draw(a, name, r->x, r->y);
}

/* Full-size mission map with an OK button (returns 0 on quit). */
static int map_screen(struct ui_assets *a, int index)
{
    int pressed = 0, hover = 0;
    for (;;) {
        SDL_Event e;
        char name[16];
        while (SDL_PollEvent(&e)) {
            float x, y;
            if (e.type == SDL_EVENT_QUIT) {
                SDL_PushEvent(&e);
                return 0;
            }
            if (e.type == SDL_EVENT_KEY_DOWN &&
                (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN ||
                 e.key.key == SDLK_KP_ENTER)) {
                return 1;
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH ||
                 e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST)) {
                return 1;
            }
            if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
                y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
                SDL_RenderCoordinatesFromWindow(a->renderer, x, y, &x, &y);
                hover = ui_point_in(&ui_map_ok, (int)x, (int)y);
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && hover) {
                    pressed = 1;
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (pressed && hover) {
                        return 1;
                    }
                    pressed = 0;
                }
            }
        }
        SDL_RenderClear(a->renderer);
        draw(a, "144", 0, 0);
        snprintf(name, sizeof name, "MISMAP%d", index);
        draw(a, name, ui_map_view.x, ui_map_view.y);
        draw_button(a, "MAPOK", pressed && hover ? 'D' : 'F', &ui_map_ok);
        SDL_RenderPresent(a->renderer);
        SDL_Delay(15);
    }
}

int ui_mission_screen(SDL_Renderer *renderer, const char *ui_dir, int initial,
                      int *value)
{
    struct ui_assets a = {0};
    struct ui_mission_state st = {0, 0};
    int hover = UI_BTN_NONE, pressed = UI_BTN_NONE, kbd_focus = UI_BTN_OK;
    int result = 0, running = 1;
    a.renderer = renderer;
    a.dir = ui_dir;
    if (!asset(&a, "142") || !asset(&a, "MISLISTDS")) {
        free_assets(&a);
        return -1;
    }
    ui_mission_select(&st, initial);
    *value = -1;
    while (running) {
        SDL_Event e;
        int row;
        char name[16];
        while (SDL_PollEvent(&e)) {
            float x = 0, y = 0;
            int mouse = 0, button;
            switch (e.type) {
            case SDL_EVENT_QUIT:
                SDL_PushEvent(&e);
                result = 0; running = 0;
                break;
            case SDL_EVENT_KEY_DOWN:
                switch (e.key.key) {
                case SDLK_UP: ui_mission_move(&st, -1); break;
                case SDLK_DOWN: ui_mission_move(&st, 1); break;
                case SDLK_PAGEUP: ui_mission_move(&st, -UI_LIST_ROWS); break;
                case SDLK_PAGEDOWN: ui_mission_move(&st, UI_LIST_ROWS); break;
                case SDLK_HOME: ui_mission_select(&st, 0); break;
                case SDLK_END: ui_mission_select(&st, UI_MISSION_COUNT - 1); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                    *value = st.selected; result = 1; running = 0; break;
                case SDLK_ESCAPE:
                    *value = -1; result = 1; running = 0; break;
                case SDLK_M:
                    if (!map_screen(&a, st.selected)) { result = 0; running = 0; }
                    break;
                default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                switch (e.gbutton.button) {
                case SDL_GAMEPAD_BUTTON_DPAD_UP: ui_mission_move(&st, -1); break;
                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: ui_mission_move(&st, 1); break;
                case SDL_GAMEPAD_BUTTON_SOUTH:
                    *value = st.selected; result = 1; running = 0; break;
                case SDL_GAMEPAD_BUTTON_EAST:
                    *value = -1; result = 1; running = 0; break;
                case SDL_GAMEPAD_BUTTON_WEST:
                    if (!map_screen(&a, st.selected)) { result = 0; running = 0; }
                    break;
                default: break;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                ui_mission_move(&st, e.wheel.y > 0 ? -1 : 1);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                x = e.motion.x; y = e.motion.y; mouse = 1;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                x = e.button.x; y = e.button.y; mouse = 2 + (e.type == SDL_EVENT_MOUSE_BUTTON_UP);
                break;
            default: break;
            }
            if (!mouse) {
                continue;
            }
            SDL_RenderCoordinatesFromWindow(renderer, x, y, &x, &y);
            button = ui_mission_button_at((int)x, (int)y);
            hover = button;
            if (mouse == 2) {
                row = ui_mission_row_at(&st, (int)x, (int)y);
                if (row >= 0) {
                    ui_mission_select(&st, row);
                }
                pressed = button;
            } else if (mouse == 3) {
                if (pressed != UI_BTN_NONE && pressed == button) {
                    if (button == UI_BTN_OK) {
                        *value = st.selected; result = 1; running = 0;
                    } else if (button == UI_BTN_CANCEL) {
                        *value = -1; result = 1; running = 0;
                    } else if (button == UI_BTN_MAP) {
                        if (!map_screen(&a, st.selected)) { result = 0; running = 0; }
                    }                   /* weapons: not implemented yet */
                }
                pressed = UI_BTN_NONE;
            }
        }
        SDL_RenderClear(renderer);
        draw(&a, "142", 0, 0);
        for (row = 0; row < UI_LIST_ROWS && st.top + row < UI_MISSION_COUNT; row++) {
            int index = st.top + row;
            draw_row(&a, index == st.selected ? "MISLISTSELDS" : "MISLISTDS", index,
                     ui_mission_list.x, ui_mission_list.y + row * UI_LIST_ROW_H);
        }
        snprintf(name, sizeof name, "MISDESC%d", st.selected);
        draw(&a, name, ui_mission_briefing.x, ui_mission_briefing.y);
        draw(&a, st.selected < UI_TRAINING_COUNT ? "MISSMLTRAINMAP1" : "MISSMLMAP1",
             ui_mission_preview.x, ui_mission_preview.y);
        for (row = 0; row < UI_BTN_COUNT; row++) {
            char state = pressed == row && hover == row ? 'D'
                       : (hover == row || (hover == UI_BTN_NONE && kbd_focus == row)) ? 'F' : 'U';
            draw_button(&a, button_stem[row], state, &ui_mission_buttons[row]);
        }
        if (ui_snapshot_path) {
            SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
            if (shot) {
                SDL_SaveBMP(shot, ui_snapshot_path);
                SDL_DestroySurface(shot);
            }
            *value = st.selected; result = 1; running = 0;
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(15);
    }
    free_assets(&a);
    return result;
}
