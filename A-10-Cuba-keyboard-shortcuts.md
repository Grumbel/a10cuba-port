# A-10 Cuba! — keyboard shortcuts

Reference for the **original Parsoft/Activision A-10 Cuba! (1996)**, arranged by
function. This is a description of the original game's controls, **not** a list
of controls already implemented by the native Linux port.

The principal keyboard chart available online was made for **Macintosh**. Its
author says that it also works well with the Windows version, but some labels
within the chart disagree with each other. The **Windows v1.2** changes below
come from the contemporary v1.2 release notes. For the Windows port, those
changes take precedence over the older Macintosh chart.

Notation:

- Keys refer to a US **QWERTY** keyboard. `1` and `Num 1` are different keys;
  `Num` means the numeric keypad. An AZERTY or other layout may differ.
- `Shift` + `,` produces `<` on a US keyboard; `Shift` + `.` produces `>`.
- `A`, `B` etc. name keys, not necessarily uppercase characters.
- `R`, `M`, `S` means **press in sequence**, not all at once. `Ctrl` + `F1`
  means **hold** Ctrl while pressing F1.
- **Mac chart** indicates a binding documented for Macintosh but not yet
  verified independently against Windows `game.hlp`/`help.hlp` or the executable.
- **v1.2** indicates a Windows-specific binding stated in the 1.2 patch notes.

## Flying the aircraft

| Key | Action | Notes |
| --- | --- | --- |
| Mouse or joystick | Pitch and roll | Mouse moves nose/roll; joystick depends on input settings |
| `-` | Reduce throttle | Main keyboard |
| `+` / `=` | Increase throttle | Main keyboard; keycap depends on layout |
| `Shift` + `-` | Throttle to minimum/off | Mac chart |
| `Shift` + `+` | Full throttle | Also confirmed in a Windows flight walkthrough |
| `Shift` + `,` (`<`) | Rudder left | Distinct from rudder *trim* |
| `Shift` + `.` (`>`) | Rudder right | Distinct from rudder *trim* |
| `/` | Center rudder | Main keyboard |
| `,` | Rudder trim left | Mac chart |
| `.` | Rudder trim right | Mac chart |
| `[` | Elevator trim up | Mac chart |
| `]` | Elevator trim down | Mac chart |
| `\` | Reset trim | Mac chart |
| `F` | Extend flaps by 10° | One increment per press |
| `D` | Retract flaps by 10° | One increment per press |
| `G` | Toggle landing gear | |
| `Space` | Ground brakes / speed brakes | Function depends on whether airborne |
| `A` | Toggle autopilot | |
| `S` | Toggle Stability Augmentation System (SAS) | |
| `Z` | Discharge left engine fire extinguisher | |
| `X` | Discharge right engine fire extinguisher | |

The Macintosh chart also lists `E` for **eject**, but elsewhere uses `E` for
**ECM**. Treat the eject keyboard binding as **unresolved** on Windows; the
cockpit's clickable ejection control is an alternative. Windows v1.2 adds
`Delete` for **cutting away the parachute after ejection** (not for ejecting).

## Views and camera

| Key | Action | Notes |
| --- | --- | --- |
| `1` | Cockpit view | Number row |
| `2` | Instrument panel view | Number row |
| `3` | Chase view | Number row |
| `4` | Friendly aircraft view | Number row |
| `5` | Enemy aircraft view | Number row |
| `6` | Tower view | Number row |
| `7` | Weapon view | Number row |
| `R`, `R` | Rear view | Two successive `R` presses; Mac chart |
| `Up` | Look up | Arrow key |
| `Down` | Look down | Arrow key |
| `Left` | Look left | Arrow key |
| `Right` | Look right | Arrow key |
| `Shift` + `Up` | Zoom in | Mac chart |
| `Shift` + `Down` | Zoom out | Mac chart |
| `Tab` | Cycle nearby aircraft | In friendly/enemy views; Mac chart specifies aircraft within 5 miles |

## HUD, cockpit and navigation

| Key | Action | Notes |
| --- | --- | --- |
| `9` | Rotate HUD mode counterclockwise | E.g. navigation / PAVE / CCIP |
| `0` | Rotate HUD mode clockwise | Main number row, **not** `Num 0` |
| `B` | Toggle radar/barometric HUD altitude | |
| `H` | RWR hand-off | Radar Warning Receiver |
| `L` | Toggle cockpit lights | |
| `Caps Lock` | Lock/unlock cockpit cursor hot-key mode | Mac chart |
| `Page Up`, `Page Down` | Adjust HUD intensity | Direction is ambiguous in the Mac chart (it calls both *decrease*); verify in Windows |
| `T` | Toggle TACAN navigation | Corroborated by Windows flight walkthrough |
| `W` | Select/cycle waypoint | Mac chart's text list; diagram instead labels `Q` as waypoint |
| `Esc` | Open tactical map | In flight |
| `N` | Advance clock by 30 minutes | Mac chart |
| `Shift` + `N` | Rewind clock by 30 minutes | Mac chart |

The Mac chart also documents the following **Mac-only modifier notation**.
Do **not** assume `Command` maps to Windows `Ctrl` until checked against the
Windows help files or binary:

| Macintosh shortcut | Action |
| --- | --- |
| `Command` + `/` | Toggle instrument panel |
| `Command` + `C` | Change HUD color |
| `Command` + `H` | Toggle HUD frame |
| `Option` (held) | Temporarily use cockpit hand/cursor rather than flight mouse |

## Weapons, countermeasures and targeting

| Key | Action | Notes |
| --- | --- | --- |
| `M` | Toggle master arm | Must be armed to fire weapons |
| `F1`–`F11` | Select/toggle weapon station 1–11 | Depending on loadout; station 11 can be an ECM pod |
| `F12` | Deselect all weapon stations | |
| `Return` / `Enter` | Select target / release selected weapon | Context-dependent; keypad Enter also documented |
| `Tab` | Cycle HUD targets | Outside friendly/enemy camera views |
| `Backspace` | Release weapon/target lock | **Windows v1.2**, replaces `Delete` |
| `Delete` | Release weapon/target lock | Older release and Mac chart; **not v1.2** |
| `C` | Toggle cannon firing rate (high/low) | Not the actual cannon trigger |
| `Num 0` | Fire GAU-8 cannon | Mac chart; on numeric keypad |
| `E` or `W` | Toggle ECM | Conflicting Mac sources: text legend says `E`, keyboard diagram says `W`; **not verified on Windows** |
| `;` / `:` | Deploy chaff | Mac diagram uses `;`, text list uses `:` (Shift+`;`) |
| `'` / `"` | Deploy flare | Mac diagram uses `'`, text list uses `"` (Shift+`'`) |
| `J` or `Command` + `J` | Jettison stores | Mac diagram/text conflict about modifier; unverified on Windows |

### Weapon release programming

The following are **sequences**, not key combinations. They are transcribed
from the Macintosh keyboard card; the Windows implementation is not yet
independently verified.

| Sequence | Action |
| --- | --- |
| `R`, `M`, `S` | Single release (`SGL`) |
| `R`, `M`, `P` | Paired release (`PRS`) |
| `R`, `M`, `J` | Jettison release (`JETT`) |
| `R`, `M`, `R`, `S` | Ripple single (`RIP SGL`) |
| `R`, `M`, `R`, `P` | Ripple paired (`RIP PRS`) |
| `R`, `/`, digit, digit | Set ripple interval (`nn` × 10 ms) |
| `R`, `C`, digit, digit | Set ripple count (`nn` stores) |

For example, `R`, `/`, `2`, `5` specifies a 250 ms ripple interval. This is
the **weapon release** sequence; it is unrelated to the double-`R` rear view.

### Numeric keypad (Mac chart)

These are **additional** keypad assignments shown on the Mac keyboard diagram.
They are not all guaranteed to work on a standard PC keypad, especially with
Num Lock or a keyboard-steering mode enabled.

| Key | Action |
| --- | --- |
| `Num 1`–`Num 9` | Select corresponding weapon station |
| `Num =` | Select station 10 (Mac extended keypad) |
| `Num /` | Select station 11 |
| `Num Clear` | Clear all stations (Mac extended keypad) |
| `Num *` | MFD off |
| `Num -` | MFD damage display |
| `Num +` | MFD TV display |
| `Num Enter` | Target selection / weapon release |
| `Num 0` | Fire cannon |

The alternative **Mac mouse/keyboard** diagram assigns `Num 7` to nose down,
`Num 5` to nose up, `Num 4` to roll left and `Num 6` to roll right instead of
their station selections. Which mode, if any, the Windows edition uses remains
to be established from the actual game.

## Multiplayer (Windows 1.2)

These commands were explicitly introduced or documented in the Windows
**v1.2 patch notes**; they operate in a network game, not necessarily in
single-player flight.

| Key | Action |
| --- | --- |
| `Ctrl` + `F1`–`F8` | Send one of eight user-defined instant messages |
| `Shift` + `F1`–`F8` | Send one of eight prerecorded audio taunts |
| `~` | Open network text chat line |
| `Enter` | Send typed chat message |
| `Esc` | Cancel chat entry |
| `Esc` | Leave network scoring screen and return to flight |
| `Space` | Activate highlighted `Reload` or `Resume` in network score screen |

The patch notes say that the score screen initially selects `Reload` if
rearming is possible, or `Resume` otherwise.

## Undocumented / cheat controls (unverified)

These are widely repeated by third-party PC cheat lists, **not** by the
Parsoft keyboard chart or the v1.2 release notes. They are included for
historical completeness, but should **not** be treated as established port
requirements without testing. The cheat sources say to enable the
**Invincible Aircraft** control setting first.

| Key | Reported effect |
| --- | --- |
| `Ctrl` + `Tab` | Increase altitude by 500 ft |
| `Ctrl` + `Tab` + `S` | Toggle slow-motion mode |
| `Ctrl` + `Tab` + `X` | Enable a free-movement/"joystick" cheat |
| `Num 8` / `Num 2` | Move forward / backward in cheat mode |
| `Num 4` / `Num 6` | Move left / right in cheat mode |
| `Num 7` / `Num 1` | Move up / down in cheat mode |

Some cheat sites also list pause-menu codes for lives, jumping and enemies.
Those appear unrelated to this flight simulator; they have deliberately **not**
been copied here.

## Known source conflicts and verification still needed

1. The Macintosh chart's **printed keyboard diagram** labels `Q` = waypoint,
   `W` = ECM, `E` = TACAN, whereas its **text legend** labels `W` = waypoint,
   `E` = ECM, `T` = TACAN and elsewhere `E` = eject. A Windows playthrough
   independently confirms `T` = TACAN. The other conflicting letter bindings
   need checking against the Windows help files.
2. Mac diagram `J` versus Mac text `Command` + `J`; `;`/`'` versus
   `:`/`"`; and both `Page Up`/`Page Down` marked as HUD intensity *decrease*
   are transcription/source ambiguities, **not** resolved PC defaults.
3. The Windows v1.2 notes explicitly override the older `Delete` target-unlock
   assignment with **`Backspace`**. `Delete` instead cuts the parachute after
   ejection. Other releases may behave differently.
4. The exact keyboard method for **ejecting**, pausing, interacting with the
   cockpit, and exiting a mission has not been verified for Windows. Do not
   silently borrow those assignments from *A-10 Attack!*, which is a different
   game, or from unsourced PC cheat tables.

The most direct next verification sources are `game.hlp` and `help.hlp` on an
original Windows CD/installation, its bundled paper reference card, and a
running Windows v1.2 game. This repository currently contains neither the
help files nor the retail key card in version control (intentionally: game
assets are not redistributed).

## Sources

- [A-10 Cuba! Keyboard Layout / Commands Quick Reference](https://russillosm.com/CubaKeyboardCard.pdf)
  — detailed Macintosh keyboard chart and release-programming sequences;
  hosted by [Steve Russillo's HOG HEAVEN](https://www.russillosm.com/a10.html),
  whose introduction explicitly notes the Windows applicability is anecdotal.
- [A-10 Cuba! Windows v1.2 release notes, reproduced in a contemporary
  COMBATSIM review](https://www.combatsim.com/memb123/archive/htm/htm_arc1/a10.htm)
  — authoritative v1.2 changes for target unlock, network keys, network
  scoring and parachute cutaway.
- [Abandonware France's Windows flight walkthrough](https://www.abandonware-france.org/ltf_abandon/ltf_jeu.php?id=3046)
  — independent checks for flaps, gear, throttle, TACAN, autopilot, HUD,
  station keys and master arm. The author's AZERTY layout and v1.0/v1.2
  differences require care.
- [A-10 Cuba! Windows help-file documentation overview](https://www.tales-from-darkenedroom.com/post/a-10-cuba-no-risk-of-a-fubar)
  — identifies `game.hlp` and `help.hlp` as the original Windows references.
- [GameFAQs PC cheat list](https://gamefaqs.gamespot.com/pc/36006-a-10-cuba/cheats)
  — secondary source for the optional, unverified cheat shortcuts only.
