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
  `IMAGE_REL_BASED_HIGHLOW` relocations after rebasing. It accepts **two PE32
  modules** (typically EXE and simulation DLL) and inventories imported DLLs.
  It refuses to enter an image with unresolved imports or TLS. It can call
  a tiny, import-free **test** PE entry directly on x86.
- `src/pe32_link.h` and `src/pe32_link.c`: the shared import/export linker.
  It looks up a DLL by its PE export name or case-insensitive filename,
  resolves exports by exact symbol name or ordinal, detects forwarded exports,
  and validates *all* imports before writing a target module's import address
  table. It never silently stubs an unknown Windows API. An optional external
  resolver can supply **explicitly implemented** platform shim addresses.
- `src/pe32_win32.h` and `src/pe32_win32.c`: a deliberately narrow `stdcall`
  `KERNEL32.dll` shim with **51** exact exports. The preloaded-module registry
  supports `LoadLibraryA`, `GetProcAddress`, and `GetModuleHandleA`; the EXE
  uses the first two to locate the simulation exports. The registry returns
  image bases as Windows HMODULEs and supports named/ordinal lookups. The
  single-thread CRT primitives are `TlsAlloc`, `TlsFree`, `TlsGetValue`,
  `TlsSetValue`, `GetLastError`, and `SetLastError`. TLS has 64 reusable slots;
  successful `TlsGetValue` clears last error even for a NULL value. This
  **does not** load binaries on demand, call `DllMain`, maintain reference
  counts, or isolate TLS and last-error state across Linux threads. The shim
  is usable for **single-thread bootstrap experiments only**.
  Ten further startup exports give the CRT a quoted guest-image command
  line, module filename lookups, a 68-byte PE32 `STARTUPINFOA`, a double-NUL
  **empty** ANSI/UTF-16 environment with matching Free calls, and Win32
  process/thread pseudo-handles and synthetic thread ID 1. These are
  **bootstrap placeholders**, not a PEB/TEB, full Windows drive mapping,
  inherited environment, real thread registry or process-launch implementation.
  Linux module filenames are represented as `C:\<basename>` to agree with
  the fixed virtual game-directory volume.
  Six other EXE imports have native implementations: `lstrlenA`, `lstrcpyA`,
  `lstrcpynA`, `lstrcatA`, `InterlockedIncrement` and
  `InterlockedDecrement`. The string helpers operate on NUL-terminated ANSI
  **bytes**, not locale-aware collation, with non-overlapping valid buffers
  required; Win32 SEH invalid-pointer recovery is absent. The counters use
  GCC sequentially consistent, atomic 32-bit operations, including wraparound.
  Locale-dependent `lstrcmpA`/`lstrcmpiA` remain unresolved rather than
  exposing unfaithful ASCII comparisons.
  `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`
  and `DeleteCriticalSection` reproduce the 24-byte Windows i386 structure's
  unlocked/owned/recursive counts for the **one synthetic Win32 thread**. The
  shim does not allocate NT debug records, contend, block, or protect data
  across real Linux threads. These functions must be replaced with genuine
  synchronization before enabling `CreateThread` or async callbacks.
- `src/pe32_heap.h` and `src/pe32_heap.c`: six further `KERNEL32.dll` entry
  points required by the original CRT: `HeapCreate`, `HeapDestroy`,
  `HeapAlloc`, `HeapReAlloc`, `HeapFree`, and `HeapSize`. A Linux `mmap`/`munmap`
  backend provides page-backed, 8-byte-aligned, split/coalescing reusable
  blocks, with zero-fill, non-growable size limits, ownership checks,
  in-place-only reallocations and teardown. This is **not** the Windows heap
  implementation: its segment reservation strategy differs, it has no
  inter-thread serialization, and it explicitly rejects executable heaps and
  exception-generating options until those semantics are implemented. The
  heap tests use an injectable memory backend on the host without 32-bit ELF.
- `src/pe32_io.h` and `src/pe32_io.c`: sixteen `KERNEL32.dll` functions:
  `GetStdHandle`, `SetStdHandle`, `SetHandleCount`, `GetFileType`, `ReadFile`,
  `WriteFile`, `CloseHandle`, `GetFullPathNameA`, `CreateFileA`, `GetFileSize`, and
  `SetFilePointer`, `GetFileTime`, `GetFileInformationByHandle`,
  `FindFirstFileA`, `FindNextFileA`, and `FindClose`.
  Synthetic Win32 handles initially map to inherited Linux
  fds 0/1/2, support rebinding standard slots, and are never passed directly
  to the Linux kernel. `CreateFileA` allocates separate Win32 handles for up
  to 64 simultaneous **read-only** regular game files. The loader derives its
  game-data root from the EXE path. `GetFullPathNameA` lexically expands paths
  into a fixed virtual `C:\` whose root is that directory, without checking
  whether files exist; it supports length probing, exact-size output and
  in-buffer file-part pointers. `OPEN_EXISTING` accepts simple relative paths
  with Windows backslash separators and paths rooted at virtual `C:\`, while
  writes, file creation, host absolute/other-drive paths, parent-directory
  traversal and unsupported flags are rejected. Neither UNC paths nor other
  drive current directories are modeled. The Linux i386 backend uses `openat`
  with `O_NOFOLLOW` on **each**
  path component, so symlinks under the game root cannot escape it; this
  bootstrap first attempts the exact Linux path, then resolves each component
  through read-only directory enumeration on `ENOENT`/`ENOTDIR`, with ASCII
  case-insensitive matching. Exact spelling takes priority; if two entries
  differ only by case and neither is exact, the lookup fails as ambiguous.
  This is not locale-aware Unicode or Windows filesystem normalization.
  File sizes and positions use Linux `fstat64`/`_llseek` for 64-bit offsets.
  File metadata uses the fixed 52-byte PE32 `BY_HANDLE_FILE_INFORMATION`
  layout, original inode numbers, link counts, permission-derived read-only
  attributes, and 100-ns `FILETIME` timestamp conversion. Because i386
  `stat64` does not provide creation/birth time, the reported creation time
  copies the last modification time; virtual volume serial is zero. These
  values are **approximations**, not a complete Windows volume implementation.
  Read-only enumeration uses separate synthetic find handles and the native
  Linux `getdents64` syscall, returning PE32 `WIN32_FIND_DATAA` records of
  exactly 320 bytes. It supports simple relative directory patterns, ASCII
  case-insensitive filename matching (`*`, `?`, and special `*.*` for all
  names), and file/directory attributes with timestamps. Each entry is
  inspected via no-follow `openat`/`fstat64`; symlinks are skipped. Searching
  within a directory also supports ASCII case-insensitive path components,
  but DOS wildcard subtleties, 8.3 aliases and directory listings via Windows
  drives other than the virtual `C:\` are not implemented. At most 32
  independent find handles are available;
  `FindClose` and shim reinitialization release them.
  Inherited streams use `read`, `write` and `fstat64`; all host tests inject
  fake I/O. `GetFileType` recognizes redirected regular files, terminals and
  pipes/sockets from the kernel's file mode. Synchronous reads return actual
  byte counts, and common Linux errors become Win32 last-error codes. Closing
  a synthetic standard handle invalidates it **without** closing the loader's
  diagnostic descriptors; closing a file handle closes its private Linux fd,
  and reinitializing the shim closes any leaked file handles. This layer does
  **not** implement Win32 sharing/locking, writable files or saves, full
  Windows path semantics, overlapped I/O, console screens, or handle duplication.
  The original PE's subsystem is Windows GUI, so real Windows standard handles
  could instead be NULL; providing inherited fds is an explicit bootstrap
  configuration, not an exact process-creation emulation.
- `scripts/build-native-pe32.sh`: builds a static i386 Linux executable via
  GCC/binutils (`-m32 -nostdlib`). It does **not** use GCC to translate the
  game's Windows x86 code; those instructions remain the original bytes.
- `flake.nix`: `nix build .#a10cuba-native` packages this bootstrap on
  `x86_64-linux`. `nix run .#a10cuba-native` defaults to mapping the two
  locally extracted, ignored original binaries (inspection only). Passing
  `-- --link` attempts import binding and is still expected to report missing
  APIs. `A10_CUBA_DATA_DIR` selects another extraction directory; explicitly
  supplied loader arguments bypass the defaults. The Nix derivation itself
  contains only the GPL source, never the copyrighted game binaries.
- `tools/pe32_imports.py`: independent, read-only, host-Python PE import and
  export scanner listing DLL names, named/ordinal functions, export RVA,
  aliases and forwarders (`--exports`).
  This works even when the host cannot execute i386 Linux binaries.
- `tests/test_pe32_link.c` and `tests/test_pe32_native.py`: C linker tests run
  with the **host compiler and UBSan** (including successful name/ordinal
  binding, fake host import resolution, preloaded `LoadLibraryA`/
  `GetProcAddress` lookups, command-line/startup records, module-path buffer
  truncation, empty environment, synthetic process/thread handles, TLS
  exhaustion/reuse/last-error contracts, private heap
  allocation/reallocation/free, bounded ANSI string helpers, atomic 32-bit
  increment/decrement, recursive single-thread critical-section bookkeeping,
  inherited/redirected standard-handle I/O and bounds/last-error contracts, and
  refusal of malformed/missing/forwarded imports), plus PE32 synthetic
  fixtures, two-module imported-function calls, dynamic-DLL lookup, TLS,
  heap, process-startup and standard-I/O imported-call execution tests.
  Actual x86 entry tests run on an i386-enabled Linux kernel or through
  `qemu-i386` and skip when neither is available.

These are **bootstrap tools, not a game runtime**. They have module-to-module
import resolution and 51 early Windows API implementations, but **no full** Win32 API
layer, Windows exception/stack/thread setup, true dynamic loading or SDL3
integration. The
mapping currently uses writable/executable pages; treat the
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
python3 tools/pe32_imports.py data/disc/A-10Cuba/A10Sim.dll --exports
./build/a10-pe32-native data/disc/A-10Cuba/A10Cuba.exe data/disc/A-10Cuba/A10Sim.dll
./build/a10-pe32-native data/disc/A-10Cuba/A10Cuba.exe data/disc/A-10Cuba/A10Sim.dll --link
```

The fourth command maps and inspects the original modules but **does not
enter the game**. The fifth additionally attempts import binding and correctly
fails for the original game because its 335 EXE and 61 DLL Win32 imports have
no Linux replacements yet. **Do not** use `--run-entry` with the original
binary until Win32 startup, COM, and CRT shims are implemented. For the current
native SDL3 bitmap probe, separately use the CMake and CTest commands in
the README.

The original disc is now preserved in the user's ChatGPT Library as
`/A-10 Cuba/A10-Cuba-original-disc.iso.zst` (archive SHA-256 is recorded in
`AGENTS.md`). It was decompressed and extracted locally into ignored `data/`;
both real PE files were inspected. The executable has **335 imported symbols
across 15 DLLs**; the simulation DLL has **61 across 2 DLLs** and exactly six
verified `SIM_*` exports. Both have relocations and no TLS directory. Details
and executable checksums are in [binary-investigation](binary-investigation.md).

Of the **396 static imported symbols** in the original EXE and DLL combined,
the 51-function shim covers **84 individual imports** (49 in the EXE,
35 in the DLL). The other **313** imported symbols remain unsatisfied.
Even though both PE files have no *static TLS directory*, they import the
`Tls*` functions for dynamically allocated runtime thread-local state.

The ChatGPT development container has GCC/binutils and Python but **no Nix,
CMake, SDL3, Wine or 32-bit libc**. The bootstrap compiles as a static i386
ELF. The container kernel refuses to execute i386 ELF (`Exec format error`),
and QEMU installation is blocked by package-manager permissions. The C linker
tests **run natively on the 64-bit host**; the **actual i386 entry execution,
relocation and imported-function call tests remain skipped**. No original
A-10 code execution or SDL3 game output is claimed.

## Additional Kernel32 shims (process, text, resources)

- `src/pe32_proc.c`: Global/Local allocation (fixed blocks, handle == pointer),
  `ExitProcess`/`Sleep` through a host backend, single-thread events,
  failing `CreateThread`, and no-op exception/`VirtualProtect` stubs.
- `src/pe32_text.c`: ordinal string compare, code-page 1252/437 (Latin-1)
  conversion, `GetStringType`, `LCMapString`, `CompareString`, minimal
  `GetLocaleInfo`, `MulDiv`, and `GetVersion` (Windows 95, `0xC0000004`).
- `src/pe32_res.c`: `FindResourceA`/`LoadResource`/`LockResource`/
  `SizeofResource` over the mapped PE resource directory (first language).

- `src/pe32_misc.c`: UTC-only `FileTimeToSystemTime`/`FileTimeToLocalFileTime`/
  `GetTimeZoneInformation` (shift-subtract 64-bit division, no libgcc), read-
  only `OpenFile`/`_lclose`/`_llseek`, global atoms, discarded
  `WritePrivateProfileStringA`, volume info and lock/EOF stubs.
- `src/pe32_sys.c`: inert ADVAPI32 registry (opens/queries report not found,
  creates/sets are discarded), COMDLG32 (dialogs cancel) and SHELL32 drag stubs.
- `src/pe32_winmm.c`: all five WINMM imports (`timeGetTime` via
  `clock_gettime`; joysticks report unplugged; `PlaySoundA` is silent).

Every KERNEL32 import of the EXE and DLL is now implemented, except real SEH
(`RaiseException`/`RtlUnwind` are stubs). The first unresolved import in
`--link` is now `comdlg32.dll!GetOpenFileNameA`; USER32 (130), GDI32 (45),
ADVAPI32, COMCTL32, CTL3D32, SHELL32, WINSPOOL, DDRAW, DSOUND and DPLAY remain.

## Next milestones (in dependency order)

1. **Original binary inventory (basic metadata complete).** Both actual files
   have verified entry RVA, preferred base, relocation/import/export directory
   sizes, DLL import counts, and DLL export names/RVAs. Remaining: import
   call-site prioritization, executable revision comparison and function-by-
   function Win32/COM behavior requirements. Keep the files ignored.
2. **Dual PE module loader (core linker and preloaded lookup implemented).**
   Both modules map; named and ordinal exports resolve, and the IAT is patched
   with complete pre-validation. A 51-function, preloaded-only Kernel32
   shim supports the EXE's eventual `LoadLibraryA`/`GetProcAddress` lookup of
   `A10Sim.dll` and basic single-thread CRT startup/TLS/error/heap handling. Host C tests
   pass. Remaining: execute the synthetic two-module, dynamic-lookup and TLS
   tests on an i386-enabled host, validate actual mapped binaries, and
   implement full Win32 loading and the remaining platform imports.
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

**Critical path:** with the original EXE/DLL now available and inventoried,
implement the actual Win32/CRT startup subset and DirectDraw-to-SDL3 adapter
needed to reach the menu. The challenge is ABI and OS semantics, not raw x86
instruction execution speed.

## References

- [Microsoft PE/COFF specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
  for section mapping, imports, exports, base relocations and TLS.
- [Microsoft TlsGetValue API](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue)
  and [SetLastError API](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setlasterror)
  for thread-local storage and Win32 last-error contracts.
- [Microsoft HeapAlloc API](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc),
  [HeapReAlloc API](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc),
  and [HeapSize API](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsize)
  for the supported private-heap subset and its intentional limits.
- [Microsoft STARTUPINFOA](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfoa),
  [GetModuleFileNameA](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulefilenamea),
  and [GetEnvironmentStrings](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getenvironmentstrings)
  for the intentionally minimal CRT startup compatibility layer.
- [Microsoft DirectDraw surface API](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nn-ddraw-idirectdrawsurface7)
  for surface concepts (the actual game uses older interface versions).
- [SDL3 surface API](https://wiki.libsdl.org/SDL3/SDL_CreateSurface)
  for the prospective indexed framebuffer replacement.
- Local investigation: [binary-investigation](binary-investigation.md),
  [interface-tracing](interface-tracing.md),
  [graphics-tracing](graphics-tracing.md),
  [Wine baseline](wine-baseline.md).
