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

The executable's PE resource tree includes custom `DIB`, `DIBDLGRES`, and `WAVE` types. `objdump -x` shows a `DIB` name table with 209 entries, including `BREIFINGS`, `AIRBARBEGIN`, and `BADGES`, plus a `WAVE` table with 62 entries. The DLL contains at least custom `CLUT` and `DATA` resource types. These are PE resources inside the binaries, separate from the disc's loose files. GNU `objdump` stopped listing the trees early on these files, so these figures are individual table counts, not complete inventories. Resource formats and runtime loading paths remain to be checked.

## Reproduce focused decompilation

After creating the analyzed project as described in the initial investigation, run the Java script from Ghidra's Script Manager with addresses as script arguments, or use the full path to `support/analyzeHeadless`:

```sh
analyzeHeadless data/ghidra-project A10 -process A10Sim.dll -noanalysis \
  -scriptPath ghidra -postScript Focus.java 10013510 10033890
```

The script prints the containing function and Ghidra's decompilation for each address. Confirm important details in the disassembly: inferred types and function boundaries can be wrong.
