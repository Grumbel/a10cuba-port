# Gamepad and joystick

The SDL host (`src/sdl_host.c`) opens the first SDL gamepad and forwards it to
the runner over the input pipe:

- **Joystick API:** `joyGetNumDevs`, `joyGetPos`, `joyGetPosEx` and
  `joyGetDevCapsA` report joystick 0 once a gamepad is present (otherwise
  `JOYERR_UNPLUGGED`). Left stick = X/Y (pitch/roll), right stick X = R
  (rudder). RT and LT are joystick buttons 1 and 2.
- **Throttle:** right stick Y holds Num+ (up) or Num- (down).
- **Keys:** buttons are translated to the keyboard shortcuts from
  `A-10-Cuba-keyboard-shortcuts.md`. LB and RB are shift keys; the layer is
  chosen when a button goes down, so releasing a shift first still releases
  the right key. The table lives in `src/gamepad_map.h` and is tested by
  `tests/test_gamepad_map.c`.

| Button | Base | LB | RB | LB+RB |
| --- | --- | --- | --- | --- |
| A | Return | F | S | F1 |
| B | Space | D | T | F2 |
| X | G | C | H | F3 |
| Y | M | A | B | F4 |
| D-pad up | Up | 1 | F12 | F5 |
| D-pad down | Down | 2 | L | F6 |
| D-pad left | Left | 3 | 9 | F7 |
| D-pad right | Right | 6 | 0 | F8 |
| RT | Num0 (cannon) | `;` (chaff) | E (ECM) | - |
| LT | Tab | `'` (flare) | Tab | - |
| Back | Backspace | J | Backspace | Backspace |
| Start | Esc | Esc | Esc | Esc |
| Stick clicks | - | - | Z / X | - |

Keyboard and mouse events are also posted as `WM_KEYDOWN`, `WM_KEYUP`,
`WM_CHAR` and mouse messages to the focus window, besides updating
`GetAsyncKeyState`.

## Registry and sound files

The game finds its sounds through
`HKCU\AppEvents\Schemes\Apps\A10Cuba\<event>\.Current`. The runner now has an
in-memory registry (`src/pe32_sys.c`) seeded from `share/sound-events.txt`
(via `scripts/gen-sound-events.sh` -> `src/pe32_sound_events.h`) with values
like `Media\EngineA-10.wav`. Changes made by the game last only for the
session. Audio output is still not implemented (DirectSound is silent).
