# A-10 Cuba! Linux port

This project is an initial investigation toward a native Linux port. The original game binaries are 32-bit x86 Windows PE files; a native port requires substantial reverse engineering and replacement of Windows APIs. There is no runnable Linux port yet.

## Status

There is **no native Linux port yet**. What exists is a reference run under Wine, reverse-engineering tooling and notes, and an early native-loader bootstrap.

**Works**

- Running the original game under Wine through an isolated launcher (`nix run .#a10cuba-wine`); see [launcher notes](docs/launcher.md) and [Wine baseline](docs/wine-baseline.md).
- Extracting your own disc image into ignored local data (`scripts/extract-disc.sh`).
- Inspecting PE resources and imports (`tools/pe_resources.py`, `tools/pe32_imports.py`).
- Mapping the EXE and simulation DLL with the freestanding i386 loader (inspection only).
- Displaying extracted indexed bitmaps with the SDL3 probe (`a10-bitmap-viewer`).
- Reading the mission databases (`Missions/*.db`) with `tools/a10db.py`: scene tree, unit placement, per-unit property/loadout tokens and path waypoint scripts. The format is documented in [docs/mission-format.md](docs/mission-format.md); `.lod` loadouts are in [docs/ui-reconstruction.md](docs/ui-reconstruction.md).
- Unit tests for the Python tools and launcher (`python3 -m unittest discover -s tests -v`). They use synthetic data, not game files.

**Does not work / not done**

- The original game does not run natively: most Windows imports are unresolved, Windows exception handling, threads, DirectDraw, DirectSound and DirectPlay are not implemented, and `--link` is expected to fail.
- The SDL3 probe is not the game and does not implement the DirectDraw interface.
- Mission format is only partly understood: position units and axes, the mission-to-`missN` mapping, the footer, vertex scaling and several tokens (`side`, `targets`, `vrandom`, `token`) are unconfirmed; the `.dmo`/`.rec` demo formats are unanalysed. See "Open items" in [docs/mission-format.md](docs/mission-format.md).
- There is no mission editor, and no writer/repacker for `.db` files yet; a byte-identical round trip must be shown before any editing.
- Wine is x86-64 Linux only, needs a graphical display, and the game's behaviour under it has not been exhaustively tested.

## Reference material

- [Original A-10 Cuba! keyboard shortcuts](A-10-Cuba-keyboard-shortcuts.md)
- [Flight, cockpit, and combat glossary](GLOSSARY.md)
- [Reverse-engineering and decompilation glossary](docs/GLOSSARY.md)
- [Native x86 execution and SDL3 replacement plan](docs/native-runtime-plan.md)

## Development environment

Install Nix with flakes enabled, then run `nix develop` from this directory. The shell supplies SDL3, C/C++ build tools, binary inspection tools, Python, and 7-Zip. Ghidra can be used separately to inspect the executable and DLL. There is no native game build yet. Run `python3 -m unittest discover -s tests -v` to run the Python tests.

## Native PE32 execution bootstrap

This is the beginning of a **direct native x86** execution path, distinct from
the existing Wine reference run and SDL3 bitmap viewer. The freestanding
Linux/i386 runner can map two PE32 modules, apply their base relocations,
resolve named/ordinal exports, and bind imports between supplied modules
without a 32-bit C library. Minimal `LoadLibraryA`/`GetProcAddress`/
`GetModuleHandleA` shims can find **preloaded** DLLs. Basic single-thread
`Tls*`, last-error, and Linux-backed private-heap functions now support CRT
bootstrap experiments. The heap adapter implements six functions used by both
original game modules, but **does not** reproduce Windows heap reservation,
threading, or structured-exception semantics.
Early process/CRT imports (`GetCommandLineA`, `GetStartupInfoA`,
`GetModuleFileNameA`, environment-string access and process/thread pseudo-
handles) also have a **single-thread bootstrap implementation**. It uses an
empty guest environment, not a real Windows process environment. Linux module
paths are presented to the game as `C:\<filename>`.
Six additional original EXE imports implement native byte-string copy/append/
length and atomic 32-bit increment/decrement; locale-aware string comparisons
and Windows exception handling remain unimplemented.
Single-threaded recursive critical-section bookkeeping also supports four
functions imported by both game modules, without pretending to provide actual
cross-thread mutual exclusion.
Seven standard-handle functions provide a Linux syscall-backed bridge for
inherited stdin/stdout/stderr and synchronous reads/writes, including Win32
file-type classification and last-error handling. `CreateFileA`, `GetFileSize`
and `SetFilePointer` additionally support ordinary **read-only game data**
under the EXE's directory. `GetFullPathNameA` supplies a lexical virtual
`C:\` directory rooted at that game-data directory, with buffer sizing and
file-part pointers. File APIs accept both relative paths and those virtual
`C:\` paths, resolve ASCII filename case when an exact Linux open fails, and
reject symlinks and `..` path components. File creation, writes, sharing
semantics, overlapped I/O and a Win32 console remain unsupported. `GetFileTime` and
`GetFileInformationByHandle` report native file metadata in Win32 structures;
creation time is approximated by the last modification time and the virtual
volume serial number is currently zero. `FindFirstFileA`, `FindNextFileA` and
`FindClose` enumerate regular files and directories with case-insensitive
ASCII `*`/`?` filename matching, including Win32's extensionless `*.*` rule.
Search directories also resolve ASCII filename case. When a directory contains
two names that differ only by case, an exact spelling wins; an ambiguous
non-exact lookup fails. Symlinks, non-ASCII case folding, 8.3 aliases and
extended DOS wildcard semantics are not emulated. Other Windows drives,
UNC paths and process-wide current-directory changes are unsupported.
It **does not resolve the
remaining Windows OS imports or execute the game**. See the
[native runtime plan](docs/native-runtime-plan.md)
for the exact scope and remaining work.

On x86-64 Linux, Nix can compile the freestanding i386 bootstrap and run its
current inspection mode. It needs no 32-bit libc or SDL3, but the kernel must
support execution of 32-bit x86 ELF files. The proprietary game binaries are
**not** included in the Nix build: extract your own disc to the ignored
`data/disc/A-10Cuba/` directory first.

```sh
nix build .#a10cuba-native         # Compile the static Linux/i386 loader
nix run .#a10cuba-native           # Map and inspect the EXE and simulation DLL
nix run .#a10cuba-native -- --link  # Report unresolved Windows imports (expected to fail)

# Override the default extracted-data directory:
A10_CUBA_DATA_DIR=/path/to/A-10Cuba nix run .#a10cuba-native

# Or pass explicit images/options to the native loader:
nix run .#a10cuba-native -- /path/to/A10Cuba.exe /path/to/A10Sim.dll
```

The default command does **not** execute the game. `--run-entry` remains an
experimental option for synthetic test images, not for the original EXE.

```sh
./scripts/build-native-pe32.sh
python3 -m unittest discover -s tests -v

# Requires your own locally extracted original disc files:
python3 tools/pe32_imports.py data/disc/A-10Cuba/A10Cuba.exe
python3 tools/pe32_imports.py data/disc/A-10Cuba/A10Sim.dll --json
python3 tools/pe32_imports.py data/disc/A-10Cuba/A10Sim.dll --exports
./build/a10-pe32-native data/disc/A-10Cuba/A10Cuba.exe data/disc/A-10Cuba/A10Sim.dll
# Attempt to bind imports, expected to fail until Win32 services are ported:
./build/a10-pe32-native data/disc/A-10Cuba/A10Cuba.exe data/disc/A-10Cuba/A10Sim.dll --link
```

The mapping command (without `--link`) is **inspection only**. `--run-entry`
is an experimental option intended for synthetic PE fixtures, **not** the game.
Executing original 32-bit code requires Linux i386 ELF support; `qemu-i386`
can run its tests if the local kernel does not support that mode. The import
scanner is host-Python and needs neither 32-bit support nor Wine.

## Run the original game in Wine

On x86-64 Linux, run from this repository directory:

```sh
nix run .#a10cuba-wine
```

On first run, the launcher uses `data/disc/` if it has been extracted, or the original ISO in the parent workspace directory. To use a different image or an extracted disc, supply one explicitly:

```sh
nix run .#a10cuba-wine -- --iso /path/to/game.iso
nix run .#a10cuba-wine -- --disc /path/to/extracted-disc
```

The launcher extracts or copies the required files at runtime and runs the original Windows executable with the pinned Wine package; the ISO is not added to the Nix store. Game files are installed under `${XDG_DATA_HOME:-$HOME/.local/share}/a10cuba-wine/game/`. The Wine prefix, private home, and writable working directory are under `${XDG_STATE_HOME:-$HOME/.local/state}/a10cuba-wine/`. The working directory links to installed files, with separate writable loadout copies; new files created there remain in state. Changes made through a link to an existing installed asset affect the data copy.

Subsequent runs reuse existing files; source arguments only affect initial setup. Wine’s private home and XDG directories prevent its default Documents links from pointing to the user’s home. The launcher disables Wine Mono and Gecko for this game to avoid their optional first-run installers. It also initializes missing game sound-event mappings in the isolated Wine registry; see [Wine sound notes](docs/wine-sound.md).

The game runs inside a Wine virtual desktop window, initially 1024×768. Wine may resize that window when the game changes display mode. Set `A10_CUBA_DESKTOP_SIZE=1280x800` before the command to choose a different initial size. The first run may take a while while Wine initializes. Wine needs a graphical display, and its prefix may include personal game state. This launcher supports x86-64 Linux; it does not provide an emulation layer for other CPU architectures.

## Native bitmap probe

The SDL3 probe displays an extracted BMP resource; it is an asset and rendering test, not the game. From `nix develop`, build it and run the tests:

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests -v
```

After extracting the disc, extract a bitmap and view it (press Escape to close):

```sh
python3 tools/pe_resources.py data/disc/A-10Cuba/A10Cuba.exe --type DIB --name BADGES --extract data/badges.bmp
./build/a10-bitmap-viewer data/badges.bmp
```

The extractor refuses to overwrite an existing output. For a headless rendered snapshot, run `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build/a10-bitmap-viewer data/badges.bmp --snapshot data/rendered.bmp`. The snapshot command overwrites its output if it exists. For indexed BMPs the probe copies pixels into a pitch-aware 8-bit surface, applies the source palette, and converts to RGBA32 when uploading to an SDL texture. It does not yet reproduce the complete DirectDraw interface or run simulation code.

## Extract the disc

Provide your own game disc image. The copy used for reverse engineering is
preserved across chats in the user's ChatGPT Library at
`/A-10 Cuba/A10-Cuba-original-disc.iso.zst` (Zstandard-compressed ISO). Once
restored locally, decompress it and extract the disc:

```sh
mkdir -p data/original
zstd -d /path/to/A10-Cuba-original-disc.iso.zst -o data/original/A10-Cuba-original-disc.iso
./scripts/extract-disc.sh data/original/A10-Cuba-original-disc.iso
```

Alternatively, with a supplied ISO in the workspace parent directory, run:

```sh
./scripts/extract-disc.sh
# Or specify another image:
./scripts/extract-disc.sh /path/to/game.iso
```

The script extracts the entire ISO to `data/disc/`, verifies key files, and refuses to overwrite an existing extraction. This local directory is ignored by Git. The game executable and data are directly available on the disc; installation is not necessary for inspection. Installation or configuration may still be needed to run the original game.

- `data/disc/A-10Cuba/A10Cuba.exe` — game executable.
- `data/disc/A-10Cuba/A10Sim.dll` — simulation DLL.
- `data/disc/A-10Cuba/Missions/`, `Media/`, and `LoadOuts/` — game data.
- Other disc files, including the installer and DirectX redistributables, remain under `data/disc/`.

## Initial findings

The supplied ISO has SHA-256 `a122877d2353ee8540011dc4c47f8a3daf495f5fe5001f660e194f8ff69ad366`. Its executable and simulation DLL are PE32 i386 binaries, dated March 1997 in their PE headers. The executable imports `DDRAW.dll`, `DSOUND.dll`, `DPLAY.dll`, and other Windows libraries; the simulation DLL imports `KERNEL32.dll` and `USER32.dll`. Inspect imports with `objdump -p data/disc/A-10Cuba/A10Cuba.exe`. Imports alone do not establish how much code is assembly or the full scope of the port.

An older Wine installation may exist in `../install-old/` on the user's
machine but is not required in a fresh checkout. Keep original game files
and extracted assets out of commits.

Further Ghidra findings and reproduction instructions are in [docs/binary-investigation.md](docs/binary-investigation.md).

Detailed simulation call-site and DirectDraw initialization notes are in [docs/interface-tracing.md](docs/interface-tracing.md).

For an isolated Wine startup baseline, see [docs/wine-baseline.md](docs/wine-baseline.md).

Graphics and surface call-site notes are in [docs/graphics-tracing.md](docs/graphics-tracing.md).
