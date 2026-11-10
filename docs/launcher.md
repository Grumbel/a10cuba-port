# Native launcher notes

`nix run . -- [--mission N] [--scale N] ...` runs the original game.

- `--mission N` picks list entry N (1-16: Training 1-4, then Missions 01-12,
  so 5 is Airstrike and 16 is Canyon Run) and skips the mission screen.
  Without it the runner shows a native mission screen (`src/ui_screens.c`)
  instead of the original dialog; the other menu dialogs are auto-accepted.
- Native screens use bitmaps extracted from the exe by
  `tools/extract_ui_assets.py` (done once by the launcher into
  `a10cuba-native/ui` in XDG data). `--ui-dir DIR` (env `A10_CUBA_UI_DIR`)
  selects them; without it the old auto-accept behaviour is used.
  `--ui-snapshot FILE` saves the first native frame as BMP and exits.
- Protocol: the runner writes a frame header with magic `0xa10f0002`
  `{screen, arg}`; the host runs the screen and answers with input record
  type 7 `{7, screen, value}` (value = list index, -1 cancel, -2 no UI;
  0x100|index = open the weapons screen for that list entry). The weapons
  screen (screen 2) first sends type 7 records `{7, 0x100|station, entry}`
  for the 11 stations, then `{7, 2, 1}` (OK) or `{7, 2, -1}` (cancel).
  `--ui-demo SCREEN --mission N` runs one screen without the game.
- Keyboard input: DirectDraw's `SetCooperativeLevel` window is treated as the
  active, focused application window (`WM_ACTIVATEAPP`, `WM_ACTIVATE`,
  `WM_SETFOCUS` are posted to it) and receives all key and mouse messages,
  with PC scan codes in `lParam`. Before this the game stayed "inactive"
  behind the auto-accepted dialogs and ignored input.

## Playing several missions

A10Sim.dll cannot build a second simulation in the same process: after End
Mission the old view still lists destroyed entities and the next activation
uses manager objects that are not initialised yet (SIGSEGV at DLL offset
0x3d6a2). The runner therefore re-executes itself (`/proc/self/exe`, same
arguments plus `--mission N`, same frame/input/audio descriptors) when a
mission is chosen after one has been played. The host keeps its pipes and just
sees the new process's frames. The weapon loadout screen is shown before the
restart; applying it in flight is still unverified.
