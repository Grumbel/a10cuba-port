# A-10 Cuba! port — reverse-engineering and decompilation glossary

Working terminology for this repository's investigation of the original
**Windows 32-bit** A-10 Cuba! executable. The audience is someone reading
`docs/`, Ghidra output, traces, and the evolving native implementation.
Unlike the [gameplay glossary](../GLOSSARY.md), these terms describe **how the
software works and how we investigate it**.

**Evidence convention:** “observed” means a fact seen in the listed binary,
API call, screenshot, or test. “Inferred” means a plausible interpretation of
such evidence. Ghidra's decompiled C-like output is **not** recovered original
source code; guessed function prototypes and names must be verified. This
distinction is especially important for `A10Sim.dll` callback structures.

## Reverse-engineering workflow

| Term | Meaning in this project |
| --- | --- |
| **ABI (Application Binary Interface)** | Binary-level rules for argument passing, register usage, stack layout, return values, and data structure layout. Necessary to make a replacement `A10Sim.dll` interface compatible. |
| **Analysis estimate** | Tool-generated result that needs validation. For example, Ghidra's number of detected functions is not a proven count of source functions. |
| **Basic block** | Straight-line instruction sequence with one entry and no internal jumps; a basic unit of a control-flow graph. |
| **Breakpoint** | Debugger stop at an instruction/address, used to inspect a live execution state. |
| **Call graph** | Network of which functions call which others; useful for locating rendering or simulation pathways. Incomplete when calls are indirect or code has not been recovered. |
| **Call site** | Instruction that invokes a function. Examining each caller helps establish actual argument types and return-value handling. |
| **Callback** | Function pointer passed to another component for it to invoke later; e.g. entries exchanged across the game/DLL boundary. |
| **Control-flow graph (CFG)** | Diagram of basic blocks and the branches connecting them inside a function. |
| **Decompiler** | Tool that translates machine instructions into approximate higher-level pseudocode. It cannot reliably recover original variable names, source layout, or types. |
| **Decompilation** | Reconstructing higher-level code from a binary. It is separate from making the reconstructed code compile or behave equivalently. |
| **Disassembler / disassembly** | Tool / output translating machine-code bytes into assembly instructions; generally closer to the original behavior than decompiler pseudocode. |
| **Dynamic analysis** | Observing the original program while it runs, e.g. with Wine logs, breakpoints, and screenshots. Complements static disassembly. |
| **Entry point** | Address where a module starts executing, or a known entry into a function/API. Does not imply its complete initialization is understood. |
| **Function boundary** | Estimated start/end of machine code belonging to one routine. Ghidra can miss routines or join/split them incorrectly. |
| **Ghidra** | Software reverse-engineering suite used here to identify functions, references, data, and tentative C-like pseudocode. |
| **Ghidra Script Manager** | Ghidra interface for running scripts such as this repo's `Focus.java` and `Report.java` against an analyzed program. |
| **Headless analysis** | Running Ghidra's `analyzeHeadless` without its GUI to import binaries or execute Java analysis scripts. |
| **Hook / interception** | Redirecting or recording a call to inspect or replace behavior. Proposed hooks are distinct from the already observed static call sites. |
| **Instrumentation** | Adding logging/counters/traces around an execution path so behavior can be measured, e.g. which surface gets locked each frame. |
| **Oracle / reference run** | The original game's measured behavior, used as a correctness reference for a replacement. A screenshot or startup observation is a narrow oracle, not proof of full equivalence. |
| **Port** | New implementation running on another platform. It can replace system APIs while preserving the original game's intended behavior. |
| **Pseudocode** | Human-readable approximation of low-level logic. Ghidra output is pseudocode even when it looks syntactically like C. |
| **Recompilation** | Building source code into machine code again. No complete original A-10 Cuba! source has been recovered here. |
| **Signature / prototype** | Function's name, argument and return types, and often calling convention. An export name alone rarely establishes the full signature. |
| **Static analysis** | Inspection of files/instructions without executing them. E.g. mapping DirectDraw vtable offsets in Ghidra. |
| **Symbol** | Name associated with code/data, from exports, debug info, or human annotations; meaningful original names are much rarer than auto-generated names. |
| **Test harness / probe** | Small program or test that isolates one component. The SDL3 bitmap viewer proves indexed-image handling, **not** a complete native game. |
| **Trace** | Ordered record of operations, addresses, parameters, or state changes from an execution. Distinguish live traces from static call-site listings. |
| **Xref (cross-reference)** | A code/data location referring to another address, e.g. a call to an imported API or a string reference. |

## Executable format, addresses, and CPU

| Term | Meaning in this project |
| --- | --- |
| **`.rsrc`** | PE section containing structured Windows resources such as images, sounds, dialogs, and custom resource types. The original executable has a particularly large `.rsrc` section. |
| **`.text`** | PE section normally holding executable machine code. Its size is not the whole executable's code/data size. |
| **32-bit / i386 / x86** | Original Windows code uses the 32-bit Intel-compatible instruction set and 32-bit pointers. This is distinct from the host's possible x86-64 process. |
| **Base relocation** | Loader correction of stored addresses when a module is mapped somewhere other than its preferred base. |
| **BSS / zero-initialized data** | Memory reserved for variables with no explicit nonzero file contents. Important when reconstructing original in-memory state. |
| **Byte / word / dword** | In this 32-bit x86 context, 8 / 16 / 32 bits. Names can differ in other contexts; check the actual type width. |
| **Calling convention** | Part of the ABI specifying how a function receives arguments and who restores the stack. The `_SIM_Init@4` naming pattern suggests 32-bit `stdcall`, but call sites remain authoritative. |
| **COFF** | Common Object File Format used with PE for Windows object files. It specifies sections, symbols, and relocation information at link time. |
| **Decorated export** | Exported name encoding calling-convention metadata. In `_SIM_ProcessEvent@16`, `@16` denotes **16 bytes** of arguments in the usual 32-bit `stdcall` decoration, **not** 16 parameters or their types. |
| **DLL (Dynamic-Link Library)** | Windows module loaded by a process; here `A10Sim.dll` supplies simulation-facing exports to `A10Cuba.exe`. |
| **Endianness** | Byte ordering in multi-byte values; x86 is little-endian. Matters when reading fields and 16/32-bit values from binary data. |
| **EXE / executable** | Main program image, here `A10Cuba.exe`, distinct from its separately loaded simulation DLL. |
| **Export** | Function or data name made available by a DLL to its consumers; this project has identified six exported `SIM_*` functions. |
| **File offset** | Byte position in the stored executable. **Not generally equal** to a memory address or RVA. |
| **Heap / allocation** | Dynamically allocated memory and its lifecycle; `SIM_Malloc`/`SIM_Free` are visible at the DLL boundary but their internal ownership rules are still under study. |
| **Hexadecimal / `0x`** | Base-16 notation used for addresses, flags, and offsets. For instance `0x20` is 32 decimal bytes. |
| **Image base (preferred)** | Virtual address where the PE asks to be loaded. The analyzed EXE prefers `0x00400000`; the DLL prefers `0x10000000`. |
| **Import** | External function or data dependency listed by a PE, such as `DirectDrawCreate` in the executable or Win32 APIs in the DLL. An import proves dependency, not its complete behavior. |
| **Import address table (IAT)** | Table of addresses the Windows loader resolves so the program can call imported functions. |
| **Indirect call** | Call via a register or memory-held function pointer rather than a literal code address; common with vtables and callbacks. |
| **Little-endian** | Least significant byte first in memory for multi-byte integers; x86's native convention. |
| **LoadLibraryA / GetProcAddress** | Win32 functions for loading a DLL and resolving exports by name; the game uses them to obtain its six simulation entry points. `A` indicates an ANSI/byte-string API variant. |
| **Memory map / address space** | Arrangement of executable, DLL, stack, heap and other regions in a process's virtual memory. |
| **Module** | Separately linked loadable binary (EXE/DLL); each module can have its own image base and exports. |
| **NUL-terminated string** | Byte string ending with zero (`\0`), used for the shared-record names such as `siminfo`. |
| **Object layout / field offset** | Position of a data member relative to the start of a struct or object. `+0x20` means 32 bytes after the selected base pointer. Choosing the correct base pointer is crucial. |
| **PE / PE32 (Portable Executable)** | Windows executable/DLL file format, with headers, sections, imports, exports, and resources. **PE32** is the 32-bit variant used by this game. |
| **Pointer** | Value referring to a memory address; original game pointers are generally 32 bits. A pointer to the middle of an object is not its owning object's base address. |
| **Register** | Small CPU storage location such as `EAX`, `ECX`, `EDX`, `ESP`, or `EIP`; assembly and calling conventions depend on their roles. |
| **RVA (Relative Virtual Address)** | Address relative to a loaded module's image base: `RVA = VA - image_base`. Example: EXE VA `0x0040885c` means RVA `0x885c`, **not** necessarily file offset `0x885c`. |
| **Section** | Named region within a PE holding related code/data. `.`-prefixed section names are conventions, not proof of actual contents. |
| **Stack / stack argument** | Last-in-first-out memory area used for return addresses, local variables, and arguments. Calls may push arguments in an order different from the written function prototype. |
| **Unaligned field** | Data beginning at an address not divisible by its natural width (e.g. a 4-byte pointer at offset `+0xf6`). May suggest packed data or a pointer into a larger structure; does **not** prove a source declaration. |
| **VA (Virtual Address)** | Memory address in the process's virtual address space, including the module base. Distinguish it from RVA and file offset. |
| **Wrapper / thunk** | Small routine that forwards or adapts a call to another routine. Several `SIM_*` exports are wrappers over internal DLL functions. |

## C++, the simulation boundary, and Win32 interfaces

| Term | Meaning in this project |
| --- | --- |
| **API (Application Programming Interface)** | Contract exposed to callers: functions, objects, argument/result conventions, and required behavior. An API name alone is not a complete behavioral specification. |
| **Callback slot** | Field containing a function address that one module writes for another to invoke. The DLL populates such slots in `loadinfo` and `siminfo`; their full semantics are unknown. |
| **COM (Component Object Model)** | Windows binary object/interface system based on interface pointers, vtables, and reference counting; DirectDraw uses COM interfaces. |
| **Constructor** | C++ initialization routine that sets object fields and usually a vtable pointer. The executable constructs host records consumed by the DLL. |
| **DirectPlay** | Microsoft's historical networking API family, imported by the game. Its presence does not establish which network features were successfully tested. |
| **DirectSound** | Historical Windows API for sound playback, imported via `DirectSoundCreate`. Distinct from sound events registered in the Windows registry. |
| **DirectX** | Microsoft's umbrella family for multimedia APIs; DirectDraw, DirectSound, and DirectPlay are distinct components used by this game. |
| **GUID (Globally Unique Identifier)** | 128-bit identifier used by COM to specify interfaces and classes. The observed GUID matches `IID_IDirectDraw2`. |
| **Host / guest (interface sense)** | In these docs, the **EXE/host** constructs records and calls the **DLL/simulation**. This has nothing to do with a virtual-machine host/guest CPU. |
| **HRESULT** | 32-bit COM result code: success/failure information. DirectDraw errors such as `DDERR_SURFACELOST` are not ordinary C `errno` values. |
| **IID (Interface ID)** | GUID identifying a particular COM interface, e.g. `IID_IDirectDraw2`; not a globally unique identifier for an individual object. |
| **IUnknown** | Base COM interface for `QueryInterface`, `AddRef`, and `Release`. The precise version of a DirectDraw interface affects its vtable layout. |
| **MinGW / `ddraw.h`** | Windows-compatible compiler/header environment; its DirectDraw definitions were used to check method offsets and constants, rather than treating guessed method names as fact. |
| **Named shared record** | Project's provisional term for EXE-created objects looked up by the DLL using names such as `siminfo`, `loadinfo`, `wayinfo`, and `panelinfo`. Their full data layouts remain unverified. |
| **Object base versus subobject** | A callback lookup may return a pointer to a name embedded *inside* a larger object. Thus an offset from the returned pointer can differ from an offset from the C++ object's start. |
| **QueryInterface** | COM method requesting an interface of a given IID from an object. The executable requests `IDirectDraw2` after `DirectDrawCreate`. |
| **Reference counting** | COM ownership mechanism through `AddRef` and `Release`. A port must preserve resource lifetimes even if it does not expose COM itself. |
| **SIM_* exports** | `SIM_Free`, `SIM_GetName`, `SIM_Init`, `SIM_Malloc`, `SIM_ProcessEvent`, and `SIM_Start`: known DLL boundary functions, **not** yet complete confirmed C signatures. |
| **Virtual function** | C++ method invoked through a table of function pointers, supporting polymorphism. An indirect call is not automatically a virtual call. |
| **Vtable (virtual function table)** | Ordered array of method pointers used by C++/COM objects. A vtable **offset is in bytes** and depends on the exact interface/version and pointer width. |
| **Win32 API** | Windows functions and interfaces such as `KERNEL32.dll`, `USER32.dll`, registry APIs, DirectDraw, and sound/networking APIs that a native Linux port must replace or bridge. |

## Graphics, bitmaps, and DirectDraw

| Term | Meaning in this project |
| --- | --- |
| **8-bit indexed / INDEX8** | Pixel stores one palette index (0–255) rather than a full RGB color. Changing the palette can recolor the same pixel bytes. |
| **Back buffer / offscreen surface** | Image memory used for drawing before copying or presenting it. The identified `+0x40` surface is an offscreen surface in one observed setup path. |
| **Bit depth / bpp** | Bits per pixel; 8 bpp indexed and 32 bpp RGBA are different storage formats. A 24-bit **X display** is yet another context. |
| **BitBlt / blit / `BltFast`** | Block transfer of image pixels from one surface to another. `BltFast` is a DirectDraw method with a narrower set of operations. |
| **BMP / DIB** | Bitmap file / Device-Independent Bitmap data. The repo's `DIB` resource type sometimes contains a full BMP header, so don't assume type name proves the exact payload format. |
| **CLUT (Color Look-Up Table)** | Common name for an indexed-color palette; also a *custom resource type name* observed in `A10Sim.dll`, whose actual contents have **not** been decoded. |
| **Color key / transparency key** | Designated pixel value treated as transparent during some blits. Different from an alpha channel. No specific color-key usage is proved by the current traces. |
| **Cooperative level** | DirectDraw setting governing relationship with the Windows desktop, e.g. normal windowed operation versus exclusive fullscreen. |
| **`DDSCAPS_*`** | DirectDraw surface capability flags, e.g. `DDSCAPS_PRIMARYSURFACE`, `DDSCAPS_OFFSCREENPLAIN`, or `DDSCAPS_VIDEOMEMORY`. Flags describe requested/available behavior, not a guarantee of actual GPU allocation. |
| **`DDERR_SURFACELOST`** | DirectDraw error indicating surface contents/memory are unavailable and may need restoration; observed in the original's error handling. |
| **`DDLOCK_WAIT` / `DDBLTFAST_WAIT`** | Flags asking DirectDraw to wait/retry for a lock or blit instead of immediately failing with a busy condition. |
| **`DDSCL_*`** | Flags to `SetCooperativeLevel`, including `NORMAL`, `FULLSCREEN`, and `EXCLUSIVE`, used in the observed setup paths. |
| **`DDSD_*`** | Flags describing which members of a DirectDraw surface-description structure are valid, such as caps, width, or height. |
| **`DDPCAPS_*`** | DirectDraw palette flags. The observed `DDPCAPS_8BIT` plus `DDPCAPS_ALLOW256` request describes a 256-entry, 8-bit palette. |
| **DirectDraw / `IDirectDraw2`** | 1990s Windows 2D display/surface API / versioned COM interface seen in the executable. Not a modern 3D rendering library. |
| **Display mode / exclusive fullscreen** | Selected screen dimensions, bit depth, etc. / mode in which the program takes exclusive control of display output. |
| **Double buffering / flip** | Drawing into a back buffer while another image is shown / exchanging which buffer is displayed. Don't infer the game uses a flip chain merely from an offscreen-to-primary blit. |
| **Frame** | One rendered image and its related update work. Static call-site inspection alone does not say which methods happen on every frame. |
| **GPU / video memory** | Graphics processor / memory intended for graphics surfaces. The game's `VIDEOMEMORY` request can fail and fall back to another allocation. |
| **Indexed surface** | Memory plane of palette indices plus palette metadata; currently emulated in the SDL3 probe before RGBA texture upload. |
| **Lock / Unlock** | DirectDraw methods to obtain/release direct access to a surface's pixel memory. **Pitch and pointer lifetime** must be respected between calls. |
| **MFD graphics / HUD graphics** | Displayed avionics elements in the game; both may use bitmaps, but their actual rendering call paths remain to be traced. |
| **Palette** | Lookup table mapping 8-bit pixel indices to RGB(A) colors. A palette update changes appearance without rewriting indices. |
| **Pitch / stride / row bytes** | Distance in **bytes** between the starts of adjacent image rows. Can be larger than width × bytes-per-pixel due to padding/alignment. Use `surface->pitch`, never assume tightly packed rows. |
| **Pixel-perfect comparison** | Equality of corresponding output pixels. Current tests establish this for particular sample BMP renders, **not** the entire game. |
| **Primary surface** | DirectDraw's main screen-facing surface, observed at screen-object offset `+0x3c` in one initialization path. |
| **Rectangle / source rect** | Region of pixels selected for a copy. DirectDraw coordinates and object-relative offsets are different concepts. |
| **Renderer / software renderer** | Component converting images/drawing commands into output pixels / version doing that processing on the CPU. The bitmap-probe verification used an SDL software renderer. |
| **RGBA32** | Four-channel 32-bit pixel encoding used by the SDL3 probe's upload texture. Byte order in memory depends on the API's chosen pixel format. |
| **SDL3 surface / texture** | SDL's CPU-visible pixel buffer / renderer-uploaded image. The probe converts INDEX8 surfaces to RGBA32 textures; this is not a drop-in reproduction of DirectDraw's COM behavior. |
| **Surface alias** | Two fields referring to the same underlying surface. The field at `+0x44` is selected from previously created surfaces after a successful lock, **not** confirmed to be a third allocation. |
| **Surface loss / Restore** | DirectDraw condition and recovery operation when previously allocated surface data is lost, e.g. after display changes. |
| **Texture upload** | Copy/update of CPU-prepared pixels into the graphics renderer's texture storage; repeat after pixel or palette changes in the current probe. |

## PE resources, sound, and local reproduction

| Term | Meaning in this project |
| --- | --- |
| **Application sound event** | Named Windows event mapped to a sound file via registry keys; the original game reads `AppEvents\Schemes\Apps\A10Cuba\...`. Distinct from direct playback of embedded sound resources. |
| **Asset / resource** | Image, sound, or other game data. A PE resource lives inside the EXE/DLL; loose disc files are separate. |
| **`CLUT`, `SPDX`, `NRCT`, `DIBDLGRES`** | Custom PE resource type names observed in the binaries. **Their exact semantics/formats have not all been established**; do not expand unknown acronyms as fact. |
| **DRI3** | X11 Direct Rendering Infrastructure extension used by graphics drivers; a startup warning about DRI3 is not itself evidence that the game failed. |
| **EGL** | Library/API for establishing rendering contexts and surfaces; the reference Wine run logged EGL warnings without preventing the game menu from appearing. |
| **JSON** | Text serialization format used by the resource-inspection tool for metadata such as offsets, RVAs and sizes. |
| **Language ID / resource name / type** | Three-level organization used by Windows resource trees. A numeric ID and textual name are different identifiers. |
| **OLE (Object Linking and Embedding)** | Older Windows object/activation infrastructure built around COM. Wine can emit OLE setup errors during prefix initialization, which need context before treating them as fatal. |
| **Registry / `HKEY_CURRENT_USER`** | Windows settings database / per-user root. Wine implements it within a prefix; the launcher creates missing application sound mappings there. |
| **Resource leaf** | A particular entry at the end of a PE resource tree, identified by type, name/ID and language, with a data RVA and size. |
| **Sound event mapping** | Association of an event such as `A-10Engine` with a WAV file path, used by Windows/Wine's application sound system. |
| **WAV / WAVE** | Common waveform audio file / PE resource type used by the game. Not proof that all sounds are read or played the same way. |
| **Wine** | Compatibility layer implementing Windows APIs on Unix-like systems; used here to run the original game as a **reference**, not the native port. |
| **Wine prefix** | Isolated directory of Wine's virtual registry, Windows files and settings. Prevents reference testing from modifying the user's ordinary Wine environment. |
| **X11** | Unix windowing protocol used by the Wine reference run's virtual display (Xvfb). Distinct from DirectDraw inside the Windows guest program. |
| **WoW64** | Windows-on-Windows 64-bit compatibility model for 32-bit Windows code; also used to describe a Wine build capable of running this 32-bit game without a separate 32-bit prefix. |

## Build, testing, and project infrastructure

| Term | Meaning in this project |
| --- | --- |
| **CMake / Ninja / CTest** | Build generator / fast build executor / CMake's test runner. They build and check the native SDL3 probe. |
| **Flake / `flake.nix` / `flake.lock`** | Nix project's outputs and pinned input definitions. They specify a reproducible development environment and runnable tools. |
| **Headless / Xvfb** | Running without a physical display / virtual X11 framebuffer used for Wine and graphics tests. A headless run can still render screenshots. |
| **Nix development shell** | Toolchain/dependencies supplied by `nix develop`; does not itself prove the original game can run. |
| **Regression test** | Check that a previously verified property remains true after changes, e.g. palette updates and pitch handling. |
| **SHA-256** | Cryptographic file hash used to identify the original disc bytes; this is a provenance check, not a legal right to distribute those bytes. |
| **Snapshot** | Captured output image or state used in comparison; may contain copyrighted game imagery and is stored in ignored `data/`. |
| **XDG data/state directories** | Conventional Linux locations for durable program data versus writable runtime/application state. The Wine launcher separates installed data from its private prefix/state. |

## Project-specific names worth recognizing

| Name | Current evidence; not a guessed full definition |
| --- | --- |
| `A10Cuba.exe` | Original Windows host executable; owns UI/graphics/platform calls and loads the simulation DLL. |
| `A10Sim.dll` | Original Windows simulation library exporting six `SIM_*` names. |
| `CDirectDrawScreen` | String observed in the executable; related to screen/interface code, but a complete original class definition has not been recovered. |
| `siminfo`, `loadinfo`, `wayinfo`, `panelinfo` | Names used to find shared records during DLL initialization; callback slots are observed in some. Their complete meanings/layouts are unknown. |
| `+0x3c`, `+0x40`, `+0x44` | Offsets in an inferred **screen object**: primary surface, offscreen surface, and selected locked-surface alias in the identified setup path. **Not global DirectDraw addresses.** |
| `+0xf6`, `+0xfa`, `+0xfe` | Unaligned callback-field offsets relative to a returned `siminfo` record pointer; avoid interpreting them as ordinary C++ member layout without more tracing. |

## Primary technical references

- [Microsoft PE/COFF specification](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
  — PE sections, image bases, file offsets, RVAs, imports and resources.
- [Microsoft DirectDraw surface API reference](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nn-ddraw-idirectdrawsurface7)
  — definitions of surface locks, blits and restoration. **API-version
  caution:** this reference illustrates concepts; the original uses older
  `IDirectDraw`/`IDirectDraw2`-era interfaces whose vtables differ.
- [Repository binary investigation](binary-investigation.md),
  [simulation interface](interface-tracing.md),
  [graphics trace](graphics-tracing.md),
  [Wine baseline](wine-baseline.md), and
  [sound registration](wine-sound.md)
  — the concrete observations that the project-specific definitions summarize.
