# Repository Guidelines

## Project Structure & Module Organization

The active project is the Git repository in `a10cuba-port/`. Its `src/` directory contains the SDL3 indexed-surface probe; `tools/` contains PE resource inspection utilities; `scripts/` contains disc extraction and the isolated Wine launcher; `tests/` holds C and Python tests; `docs/` records reverse-engineering and runtime findings; and `share/` contains the sound-event map used by the launcher. `data/` is local, ignored runtime/extraction data.

At the workspace root, `A-10 Cuba! (1996)(Activision).iso` is the original disc image and `install-old/` is an older installation kept for reference. Treat these binaries, archives, registry exports, and game assets as read-only inputs. Do not commit copyrighted game data or extracted files; check distribution rights before sharing them.

## Build, Test & Run

Run commands from `a10cuba-port/`. `nix develop` opens the pinned development environment. `./scripts/extract-disc.sh` extracts the default workspace ISO into ignored `data/disc/` (or pass an ISO path). `nix run .#a10cuba-wine` launches the original Windows game in a Wine virtual desktop; use `-- --iso PATH` or `-- --disc PATH` to select media. Game files live in XDG data at `a10cuba-wine/`; the Wine prefix and writable user state live in XDG state at the same app directory.

Build the SDL3 probe with `cmake -S . -B build -G Ninja && cmake --build build`. Run C tests with `ctest --test-dir build --output-on-failure` and Python tests with `python3 -m unittest discover -s tests -v`. The probe is not the game and does not yet implement the full DirectDraw interface.

## Coding Style & Naming

Follow nearby conventions: C uses clear `snake_case` identifiers and four-space indentation; Python uses standard four-space indentation. Preserve the spelling and case of legacy asset paths. Keep shell launchers safe for paths with spaces, quote expansions, and avoid modifying user data outside the documented XDG locations.

## Testing Guidelines

Add C tests under `tests/` and register them through CMake/CTest; Python tests use `test_*.py` and `unittest`. Run both suites for changes affecting shared functionality. For launcher changes, also verify `--help` and, when practical, a Wine run under a graphical session or Xvfb; report environmental limits.

## Commits & Pull Requests

Use short, imperative commit subjects (for example, `Keep Wine state under XDG state`). Pull requests should explain behavior, list verification and dependencies, link issues when available, and include screenshots for visible changes. Never include the ISO, extracted game files, Wine prefixes, or save data.
