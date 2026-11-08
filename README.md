# A-10 Cuba! Linux port

This project is an initial investigation toward a native Linux port. The original game binaries are 32-bit x86 Windows PE files; a native port requires substantial reverse engineering and replacement of Windows APIs. There is no runnable Linux port yet.

## Development environment

Install Nix with flakes enabled, then run `nix develop` from this directory. The shell supplies SDL3, C/C++ build tools, binary inspection tools, Python, and 7-Zip. Ghidra can be used separately to inspect the executable and DLL. There is no native game build yet. Run `python3 -m unittest discover -s tests -v` to test the PE resource reader.

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

The extractor refuses to overwrite an existing output. For a headless rendered snapshot, run `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software ./build/a10-bitmap-viewer data/badges.bmp --snapshot data/rendered.bmp`. The snapshot command overwrites its output if it exists. The probe uses SDL's BMP loader and texture conversion; it does not yet reproduce DirectDraw surface behavior or run simulation code.

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
