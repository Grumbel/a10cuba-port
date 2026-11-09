# Native x86 execution and SDL3 replacement plan

## Target and tradeoff

The desired runtime is **the original Windows PE32/i386 machine code executed
directly by an x86 Linux CPU**, coupled to replacement Linux services and an
SDL3 graphics/input/audio frontend. This avoids interpreting or emulating
every guest instruction with Unicorn or QEMU. An eventual source-level
decompilation could replace parts of the PE image with compiled C/C++ without
changing the behavior contract.

We should not mistake *CPU-native execution* for *OS-native execution*:
the original code still expects 1990s Win32, COM/DirectDraw, C runtime,
Windows thread state, and DLL-loading semantics. These must be recreated or
replaced. Wine already demonstrates CPU-native execution on Linux, but its
large Win32 compatibility implementation is not the proposed final SDL3
backend. Use Wine as a **behavioral oracle**, not as proof that the native
adapter is complete.

On x86-64 Linux a **separate 32-bit process** is the straightforward way to
execute original i386 code. We cannot just cast a PE32 function pointer from
a 64-bit SDL3 application and call it: code, stack conventions, pointer sizes
and CPU mode differ. Prefer a 32-bit SDL3 frontend in-process for lowest
overhead when the toolchain is ready. A 32-bit native worker with a 64-bit SDL3
renderer via shared memory/IPC is a fallback if 32-bit graphics dependencies
become impractical. This is an architecture choice still to be benchmarked.

## Current, tested bootstrap

The repository now has:

- `src/pe32_start.S` and `src/pe32_native.c`: a **freestanding Linux/i386
  loader** built from C and assembly, with no 32-bit glibc requirement.
  It validates PE32/i386 headers and sections, maps the original image at
  its preferred address when possible, copies section bytes and applies
  `IMAGE_REL_BASED_HIGHLOW` relocations after rebasing. It inventories imported
  DLL names and refuses to invoke the entry point while import or TLS
  dependencies remain unresolved. It can call the entry of a tiny,
  import-free **test** PE image directly on x86.
- `scripts/build-native-pe32.sh`: builds a static i386 Linux executable via
  GCC/binutils (`-m32 -nostdlib`). It does **not** use GCC to translate the
  game's Windows x86 code; those instructions remain the original bytes.
- `tools/pe32_imports.py`: independent, read-only, host-Python PE import
  scanner listing DLL names, imported functions, ordinal imports, and hints.
  This works even when the host cannot execute i386 Linux binaries.
- `tests/test_pe32_native.py`: synthetic fixtures for import enumeration and
  raw x86 entry/rebasing/denial behavior. It runs execution tests on an
  i386-enabled Linux kernel or through `qemu-i386`, and skips those tests when
  neither option is available.

These are **bootstrap tools, not a game runtime**. They have **no** PE import
resolution, Win32 API layer, Windows exception/stack/thread setup, or SDL3
integration. The mapping currently uses writable/executable pages; treat the
runner as a development experiment for trusted binaries, **not** a sandbox or
secure loader for files from strangers. `--run-entry` should only be used with
the supplied synthetic fixtures at this stage.

## Local development commands

The normal project environment is `nix develop` on NixOS/Linux. Its current
GCC/binutils/Python tools are sufficient to build the **freestanding** stage:

```sh
./scripts/build-native-pe32.sh
file build/a10-pe32-native
python3 -m unittest discover -s tests -v
```

If your kernel enables i386 ELF execution, the new tests invoke the bootstrap
directly. Otherwise install an optional `qemu-i386` user-mode runner **for
tests only**; the final runtime still targets native CPU execution. The build
does not need Wine or a game disc. For PE header/import inspection on a disc
you own:

```sh
python3 tools/pe32_imports.py data/disc/A-10Cuba/A10Cuba.exe
python3 tools/pe32_imports.py data/disc/A-10Cuba/A10Sim.dll --json
./build/a10-pe32-native data/disc/A-10Cuba/A10Cuba.exe
```

The last command maps and inspects the original image but **does not enter
the game**; omitting `--run-entry` is deliberate. It only reports DLL names,
not resolved imports. For the current native SDL3 bitmap probe, separately
use the existing CMake and CTest commands in the README.

The ChatGPT development container used for this milestone had GCC/binutils
and Python but **no Nix, CMake, SDL3, Wine, game ISO, or 32-bit libc**. The
bootstrap compiled as a statically linked i386 ELF. The container's kernel
refused to execute it (`Exec format error`), and a test-only QEMU installation
was blocked by package-manager permissions. Thus the **Python parser tests
ran**, while the **actual entry execution and relocation tests were skipped**.
Neither an original A-10 binary run nor SDL3 game output is claimed.

## Next milestones (in dependency order)

1. **Original binary inventory.** With a privately supplied original disc,
   record import tables, export tables, TLS directories, relocations, section
   layout and entrypoints of both `A10Cuba.exe` and `A10Sim.dll`. Keep binary
   data ignored; commit only textual findings. Validate against Windows 1.2
   and the existing Ghidra reports. The project currently knows six DLL
   `SIM_*` exports and has already traced a first DirectDraw setup path.
2. **Dual PE module loader.** Load the EXE and simulation DLL at their
   preferred bases (or rebase both), resolve DLL exports, and populate import
   address tables. Reject rather than mis-execute unsupported import types,
   forwarded exports, TLS, or SEH until implemented. Start with synthetic
   modules that make calls through an import thunk.
3. **Win32/CRT startup shim.** Implement the specific Windows imports the
   game actually calls, including allocation, file paths, registry settings,
   timing, thread-local state, events, window/input messages, and DLL
   initialization. Use the import inventory to **prioritize actual call
   sites**, not the entire Win32 API. Respect 32-bit x86 calling conventions,
   C++ vtables, structure packing, and callback lifetimes.
4. **DirectDraw-to-SDL3 adapter.** Implement the observed `IDirectDraw2`
   setup, primary/offscreen surfaces, 8-bit palettes, `Lock`/`Unlock`,
   `BltFast`, display mode changes, surface-loss behavior, and pixel pitch.
   Preserve COM interface shapes required by the existing machine code,
   while using SDL3 indexed surfaces/textures behind those interfaces.
   Compare known output frames from the Wine reference process.
5. **Input, sound, and network.** Bridge keyboard/mouse/joystick and joystick
   axes; replace DirectSound and Windows app-event WAV playback with SDL3
   audio. DirectPlay/networking can be deferred until single-player flight
   works, but the game may still need safe startup stubs for it.
6. **Simulation handoff.** Invoke `SIM_Init`, `SIM_Start` and
   `SIM_ProcessEvent` under the true host ABI with recovered shared-record
   layouts. Compare menu entry, training mission, HUD and control responses
   against the Wine baseline. Avoid naming fields beyond their evidence.
7. **Performance and portability.** Profile frame time, syscalls, memory
   copies, and rendering. Prefer direct indexed framebuffer uploads once
   per changed frame, no per-instruction emulation. Later, replace verified
   functions with recompilable C/C++ for 64-bit, Windows and other CPUs.

**Critical path:** getting the private original EXE/DLL available to a local
test run, inventorying imports, then implementing the *minimal* Win32 and
DirectDraw subset to reach the menu. The challenge is ABI and OS semantics,
not raw x86 instruction execution speed.

## References

- [Microsoft PE/COFF specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
  for section mapping, imports, exports, base relocations and TLS.
- [Microsoft DirectDraw surface API](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nn-ddraw-idirectdrawsurface7)
  for surface concepts (the actual game uses older interface versions).
- [SDL3 surface API](https://wiki.libsdl.org/SDL3/SDL_CreateSurface)
  for the prospective indexed framebuffer replacement.
- Local investigation: [binary-investigation](binary-investigation.md),
  [interface-tracing](interface-tracing.md),
  [graphics-tracing](graphics-tracing.md),
  [Wine baseline](wine-baseline.md).
