# Simulation and graphics interface tracing

Addresses refer to the disc binaries and preferred image bases documented in [binary-investigation.md](binary-investigation.md). These are static observations, not verified C declarations or a complete interface inventory.

## Simulation loader and startup

At `0x00414fe0`, the loader stores the resolved function pointers in an object: `Free` at `+0x0c`, `GetName` at `+0x10`, `Init` at `+0x14`, `ProcessEvent` at `+0x18`, `Malloc` at `+0x1c`, and `Start` at `+0x20`. The enclosing object's constructor places this loader at `+0x18`, so the corresponding pointers appear at offsets `+0x24` through `+0x38` in that object. This relationship is necessary to distinguish these indirect calls from unrelated virtual calls.

At `0x0041a6cd`, the executable calls `Init` with the address of a larger host object's field at `+0x20`; the return value is tested. After another host virtual call succeeds, `Start` is called at `0x0041a6e4` with a separate value and its return is also tested. The exact structure and ownership remain unknown. Inside the DLL, `Init` saves its argument and invokes `0x10033890`, which iterates a table of objects and calls the first virtual method of each.

## Event calls

The executable makes these calls through the `ProcessEvent` pointer, listed in argument order rather than push order:

| Call site | Four 32-bit stack arguments | Observation |
| --- | --- | --- |
| `0x00419941` | `0`, `1`, `[global object + 0x2c]`, `[global object + 0x30]` | The third value is checked for nonzero before the call. |
| `0x00419b52` | `1`, `0`, `0`, `0` | An alternate path in the same routine. |

The exported wrapper forwards the arguments to `0x10013510`. When argument 2 is nonzero, this function stores argument 3 as a 32-bit value and the low 16 bits of argument 4 at offsets `+2` and `+6` of a global structure, then passes argument 3 to another routine. It calls `0x100329d0` with argument 2 and, when argument 1 is nonzero, calls `0x10032690`. These branches suggest different processing modes; their semantic names and the types of the last two arguments remain unconfirmed. The internal routine also contains a special two-iteration path controlled by another global structure.

## DirectDraw initialization

The code at `0x00408850` calls `DirectDrawCreate(NULL, &object, NULL)` at `0x0040885c`. It then calls the returned interface's `QueryInterface` with the GUID stored at `0x0044c770`, which matches `IID_IDirectDraw2` (`B3A6F3E0-2B43-11CF-A2DE-00AA00B93356`). In the successful path it invokes two `IDirectDraw2` methods:

| Call site | Vtable offset | Method and observed arguments |
| --- | --- | --- |
| `0x00408883` | `0x50` | `SetCooperativeLevel(NULL, DDSCL_NORMAL)` (`DDSCL_NORMAL` is `0x8`). |
| `0x0040889a` | `0x20` | `EnumDisplayModes(0, NULL, this, callback)` with callback at `0x00408bb0`. |

Names and offsets were checked against the locally installed MinGW `ddraw.h`. These are only initialization calls; surface creation, blitting, palettes, and fullscreen paths still need tracing before designing an SDL3 implementation.

## Embedded resources

The executable's PE resource tree includes custom `DIB`, `DIBDLGRES`, and `WAVE` types. `objdump -x` shows a `DIB` name table with 209 entries, including `BREIFINGS`, `AIRBARBEGIN`, and `BADGES`, plus a `WAVE` table with 62 entries. The DLL contains at least custom `CLUT` and `DATA` resource types. These are PE resources inside the binaries, separate from the disc's loose files. GNU `objdump` stopped listing the trees early on these files; the complete inventory is now documented in [graphics-tracing.md](graphics-tracing.md). Resource formats and runtime loading paths remain to be checked.

## Reproduce focused decompilation

After creating the analyzed project as described in the initial investigation, run the Java script from Ghidra's Script Manager with addresses as script arguments, or use the full path to `support/analyzeHeadless`:

```sh
analyzeHeadless data/ghidra-project A10 -process A10Sim.dll -noanalysis \
  -scriptPath ghidra -postScript Focus.java 10013510 10033890
```

The script prints the containing function and Ghidra's decompilation for each address. Confirm important details in the disassembly: inferred types and function boundaries can be wrong.

## Initialization table follow-up

The saved `SIM_Init` argument is used by `0x10033820` to scan 64 four-byte pointers at offsets `0x00`–`0xfc`. Each non-null pointer is compared as the beginning of a NUL-terminated byte string against a requested name. Callers request names including `siminfo`, `loadinfo`, `wayinfo`, and `panelinfo`. This is more than a table of plain strings: after looking up `siminfo` and `loadinfo`, callers write function addresses into fields of the returned objects (for example `loadinfo + 0x18` at `0x1001318f`). Thus these appear to be named shared records with callbacks. Their complete layouts, ownership, and calling conventions remain unknown.

The DLL's start routine also reads a 32-bit field at initialization argument offset `0x102` and passes it to routines at `0x1001d850` and `0x1001daf0`. Its unaligned offset and purpose need verification against the host's construction code; do not assume a native C layout from these offsets.


## Host record construction and callback slots

The executable constructs several matching records. `0x004012a0` initializes `wayinfo`, `0x00418180` initializes `siminfo`, and `0x0041e8b0` initializes `loadinfo`. In each of those constructors the name starts at host object offset `+0x04`, immediately after a C++ virtual-table pointer. The DLL's lookup returns a pointer to the name at the beginning of the shared record, so offsets measured from that pointer differ by four bytes from offsets measured from the enclosing host object. The `panelinfo` name is embedded deeper within its host object; do not generalize the four-byte adjustment to every record.

The DLL initializer at `0x10013180` looks up `loadinfo` and stores nine DLL function addresses into consecutive 32-bit slots at record offsets `+0x18` through `+0x38`. For example `+0x18` receives `0x10013200` and `+0x28` receives `0x10013310`. The equivalent `siminfo` initializer at `0x100075a0` stores callbacks at unaligned record offsets including `+0xf6`, `+0xfa`, and `+0xfe`. These offsets are from the returned shared-record pointer and are specific to the original 32-bit layout. Several callback entry points were not recovered automatically as Ghidra functions; inspect their instructions and calling sites before assigning signatures or relying on a decompiler.
