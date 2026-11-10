# Native UI reconstruction

Goal: replace the game's MFC dialogs (main menu, mission list, weapon
loadout, mission map and pause screen) with native screens assembled from the
original bitmaps, without emulating the Windows GUI.

`tools/extract_ui_assets.py A10Cuba.exe --out data/ui` writes every `DIB`
resource as `data/ui/bmp/NAME.bmp` and the dialog templates as
`data/ui/dialogs.json` (units: dialog units, about 1.5 px horizontally and
2 px vertically at the game's 640x480 layout; the backgrounds are bitmaps).
The output is copyrighted and stays in the ignored `data/` directory.

## Dialogs (resource type 5)

| ID | Screen | Notable controls |
| --- | --- | --- |
| 102 | Main menu | MAINMISSION, MAINNET (5), MAINPREF (4), MAINCREDIT (6), MAINQUIT (1136); background bitmap 109-class (`MAINQUITA` strip) |
| 185 | Mission list | list 3 (owner-drawn), MISWEAPONS 1057, MISMAP 1058, MISCANCEL 2, MISOK 1, preview/briefing static 1096 |
| 188 | Weapon loadout | 11 station combos 1025-1035, comments 1122, WEPDEFLOAD 1005, WEPUNLOAD 1004, WEPLOAD 1117, WEPSAVE 1116, fuel static, WEPOK/WEPCANCEL |
| 186 | In-game mission map (Esc) | MAPRELOAD 1020, ENDMISSION 1055, MAPOK 1, map static 1120 |
| 219 | Pause/score screen | RESUME 1087, RELOAD 1020, ENDMISSION 1055 |
| 224 | Map only | MISMAPOK, map static 1120 |
| 190-193 | Preference pages | Combat, Sounds, Graphics, Controls (Device radios 1124/1054/1126) |
| 217 | Message box | MESYES/NO/ABORT/RETRY/IGNORE/OK/CANCEL |

## Bitmaps

- Full-screen 640x480 backgrounds: `109` main menu, `140` weapons, `141`
  network, `142` missions, `143` scores, `198`/`210` plain panels, `229`
  logo, `121` credits.
- Buttons come in `NAME` + `D`/`F`/`U` (down/focus/up) variants, for example
  `MISOKD`, `WEPLOADU`.
- The mission list is one strip: `MISLISTDS` (normal) and `MISLISTSELDS`
  (selected), 16 titles of 271x17 px side by side. `MISDESC0-15` (286x295)
  are the pre-rendered briefing texts, `MISSMLMAP1` the 272x187 overview and
  `MISMAP0-15` (608x408) the full mission maps; `MAPWIN`/`MAPLOSE` are the
  end-of-mission variants.
- Weapons: `WEPSTAnDS`/`WEPSTAnSELDS` are 15 px high name strips per station
  type, `WEP0-17` (229x277) the weapon pictures, `WEPDESCDS` the 18 matching
  descriptions side by side, `WEPFUELBAR*` the fuel gauge.
- `.lod` files in `LoadOuts/` hold the default loadout and briefing text.

## Plan

1. Boundary: the runner already recognises dialogs by template ID in
   `CreateDialogIndirectParam`. Replace each recognised ID with a native
   screen drawn into the game's surface (or a host overlay), and hand the
   result back through the messages the game code expects (`LB_GETCURSEL`,
   combo selections, `WM_COMMAND` with the button ID).
2. Mission list first (replaces the `LB_GETCURSEL` workaround), then the
   loadout screen, then the Esc map and pause screen.
3. Unknowns to resolve by tracing the game's dialog procedures: how the
   loadout is read back (station combos, fuel, `.lod` save/load) and how the
   map screen is told which mission to draw.

## Status

**Mission screen** (dialog 185): native (`src/ui_layout.c` logic,
`src/ui_screens.c` rendering). Keys, gamepad and mouse work. Cancel returns
to the main menu, which is still auto-driven. See `docs/launcher.md` for the
runner/host protocol.

**Weapons loadout** (dialog 188): native, opened from the mission screen
(Weapons button, `W`, gamepad Y). Findings:

- `LoadOuts/*.lod` header: 11 bytes of global weapon ids (one per station,
  symmetric 1 2 3 4 5 6 5 4 3 2 1 pylon types), a little-endian dword of fuel
  in pounds, then an MFC CString (`0xff`, u16 length) with the briefing text.
- Global weapon ids match `WEPDESCDS`/`WEP0-17`: 0 empty, 1 AIM-9L, 2 ALQ-119,
  3-5 AGM-65B/D/E, 6 AGM-88A, 7 Hellfire, 8 HVR pod, 9 Durandal, 10 Mk82,
  11 Snakeye, 12 Mk82 Paveway, 13 Mk83, 14 Mk83 Paveway, 15 Mk84,
  16 Mk84 Paveway, 17 Mk20.
- Each pylon type shows a subset in the order of its `WEPSTAnDS` strip
  (212x15 entries); the combo item index is the strip entry.
- The game's OK handler reads the 11 combos with `CB_GETCURSEL` sent to
  hwnd 0 (the MFC objects are unattached), in station order. The runner answers
  those from the native screen (`pe32_wnd_set_station`), as it does for the
  mission list's `LB_GETCURSEL`.
- Not verified: that the sim really uses the chosen loadout in flight; fuel is
  shown but not editable, LOAD/SAVE and quantities are not implemented.
