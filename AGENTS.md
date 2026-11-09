# Repository Guidelines

## Project Structure & Module Organization

The active project is the Git repository in `a10cuba-port/`. Its `src/` directory contains the SDL3 indexed-surface probe and the experimental freestanding Linux/i386 PE32 loader; `tools/` contains PE resource/import inspection utilities; `scripts/` contains disc extraction, native bootstrap building, and the isolated Wine launcher; `tests/` holds C and Python tests; `docs/` records reverse-engineering and runtime findings; and `share/` contains the sound-event map used by the launcher. `data/` is local, ignored runtime/extraction data.

The original disc is preserved across chats as **`A10-Cuba-original-disc.iso.zst`**
in the user's ChatGPT Library at **`/A-10 Cuba/`** (use that exact name to
search). It is a
Zstandard-compressed ISO 9660 disc image, SHA-256
`0713be7f01bf08fc1876f9bd4dac080bf5524ddaa2c083d04bbb272069cfc89f`.
For development, restore/decompress it into ignored
`data/original/A10-Cuba-original-disc.iso`, then run `scripts/extract-disc.sh`
with that path to create ignored `data/disc/`. If the file is missing after a
chat restart, **find the existing Library file by its exact title**; do not
request another upload or assume local scratch data survived. The external
workspace ISO and `install-old/` mentioned by older notes may not exist.
Treat these binaries, archives, registry exports, and game assets as read-only
inputs. Do not commit copyrighted game data or extracted files; check
distribution rights before sharing them.

## Build, Test & Run

Run commands from `a10cuba-port/`. `nix develop` opens the pinned development environment. `./scripts/extract-disc.sh` extracts the default workspace ISO into ignored `data/disc/` (or pass an ISO path). `nix run .#a10cuba-wine` launches the original Windows game in a Wine virtual desktop; use `-- --iso PATH` or `-- --disc PATH` to select media. Game files live in XDG data at `a10cuba-wine/`; the Wine prefix and writable user state live in XDG state at the same app directory.

Build the SDL3 probe with `cmake -S . -B build -G Ninja && cmake --build build`. Run C tests with `ctest --test-dir build --output-on-failure` and Python tests with `python3 -m unittest discover -s tests -v`. Build the freestanding i386 loader with `./scripts/build-native-pe32.sh`; use `tools/pe32_imports.py` to inspect a locally supplied game binary. The SDL3 probe and PE32 bootstrap are not the game; Windows imports, process startup, DirectDraw and simulation interfaces still need work. Follow `docs/native-runtime-plan.md` and do not claim native execution tests passed when host i386 ELF support or `qemu-i386` is absent.

`nix build .#a10cuba-native` packages the freestanding i386 loader on x86-64 Linux, while `nix run .#a10cuba-native` runs the non-executing PE32 mapping inspection on locally extracted game files. `nix run .#a10cuba-native -- --link` attempts import binding and is still expected to fail on missing Win32 APIs. The Nix build never includes proprietary files.

## Coding Style & Naming

Follow nearby conventions: C uses clear `snake_case` identifiers and four-space indentation; Python uses standard four-space indentation. Preserve the spelling and case of legacy asset paths. Keep shell launchers safe for paths with spaces, quote expansions, and avoid modifying user data outside the documented XDG locations.

## Testing Guidelines

Add C tests under `tests/` and register them through CMake/CTest; Python tests use `test_*.py` and `unittest`. Run both suites for changes affecting shared functionality. For launcher changes, also verify `--help` and, when practical, a Wine run under a graphical session or Xvfb; report environmental limits.

## Commits & Pull Requests

Use short, imperative commit subjects (for example, `Keep Wine state under XDG state`). Pull requests should explain behavior, list verification and dependencies, link issues when available, and include screenshots for visible changes. Never include the ISO, extracted game files, Wine prefixes, or save data.

## Handoff and Bundles

Deliver changes **only as one Git bundle**; do not attach individual source files,
patch files, or separate bundles. Each new bundle must contain complete history
from the project's existing base, including all earlier work, and supersedes
the previous bundle. Keep one current bundle for the handoff, verify it with
`git bundle verify`, and report the commit it contains. Do not create parallel
or orphan histories, force-push, or commit proprietary game content. References
to relevant source paths are fine in the summary, but the Git bundle is the
only file to deliver.
