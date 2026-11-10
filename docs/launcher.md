# Native launcher notes

`nix run . -- [--mission N] [--scale N] ...` runs the original game.

- `--mission N` picks mission N (1-based, in the order of the mission list;
  the disc has 12 loadouts, `Default01AirStrike` to `Default12CanyonRun`).
  The launcher auto-accepts the menu dialogs; the runner answers the mission
  dialog's list-selection query with N. Without the option the first
  mission starts.
- Keyboard input: DirectDraw's `SetCooperativeLevel` window is treated as the
  active, focused application window (`WM_ACTIVATEAPP`, `WM_ACTIVATE`,
  `WM_SETFOCUS` are posted to it) and receives all key and mouse messages,
  with PC scan codes in `lParam`. Before this the game stayed "inactive"
  behind the auto-accepted dialogs and ignored input.
