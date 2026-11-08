# A-10 Cuba! Linux port

This project is an initial investigation toward a native Linux port. The original game binaries are 32-bit x86 Windows PE files; a native port requires substantial reverse engineering and replacement of Windows APIs. There is no runnable Linux port yet.

## Development environment

Install Nix with flakes enabled, then run `nix develop` from this directory. The shell supplies SDL3, C/C++ build tools, binary inspection tools, Python, and 7-Zip. Ghidra can be used separately to inspect the executable and DLL. There is no native game build yet. Run `python3 -m unittest discover -s tests -v` to test the PE resource reader.

## Run the original game in Wine

On x86-64 Linux, run from this repository directory:

```sh
nix run .#a10-cube-wine
```

On first run, the launcher uses `data/disc/` if it has been extracted, or the original ISO in the parent workspace directory. To use a different image or an extracted disc, supply one explicitly:

```sh
nix run .#a10-cube-wine -- --iso /path/to/game.iso
nix run .#a10-cube-wine -- --disc /path/to/extracted-disc
```

The launcher extracts or copies the required files at runtime and runs the original Windows executable with the pinned Wine package; the ISO is not added to the Nix store. Game files are installed under `${XDG_DATA_HOME:-$HOME/.local/share}/a10-cuba-wine/game/`. The Wine prefix, private home, and writable working directory are under `${XDG_STATE_HOME:-$HOME/.local/state}/a10-cuba-wine/`. The working directory links to installed files, with separate writable loadout copies; new files created there remain in state. Changes made through a link to an existing installed asset affect the data copy.

A previous launcher’s working directory is preserved in place, including any saves, while its game files are copied to the data directory. Subsequent runs reuse existing files; source arguments only affect initial setup. Wine’s private home and XDG directories prevent its default Documents links from pointing to the user’s home. The launcher disables Wine Mono and Gecko for this game to avoid their optional first-run installers. It also initializes missing game sound-event mappings in the isolated Wine registry; see [Wine sound notes](docs/wine-sound.md).

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

Provide your own game disc image. With the supplied image in the workspace parent directory, run:

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

The older Wine installation in `../install-old/` is reference material and contains differently dated binaries. Keep original game files and extracted assets out of commits.

Further Ghidra findings and reproduction instructions are in [docs/binary-investigation.md](docs/binary-investigation.md).

Detailed simulation call-site and DirectDraw initialization notes are in [docs/interface-tracing.md](docs/interface-tracing.md).

For an isolated Wine startup baseline, see [docs/wine-baseline.md](docs/wine-baseline.md).

Graphics and surface call-site notes are in [docs/graphics-tracing.md](docs/graphics-tracing.md).
