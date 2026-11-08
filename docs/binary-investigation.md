# Initial binary investigation

These observations apply to the extracted disc image identified in the README. Addresses are virtual addresses at each PE's preferred image base: `0x00400000` for the executable and `0x10000000` for the DLL. They may differ for other releases or when a module is relocated.

Ghidra 12.0.4 analyzed both PE32 i386 files. Automatic analysis found 2,527 functions in `A10Cuba.exe` and 2,258 in `A10Sim.dll`; these are analysis estimates, not validated function counts. The DLL analysis emitted an unreadable-address warning at `0x1008d774`. Some executable instructions were not associated with recovered functions, so absence of a function or reference is not proof of absence of code. No original source code or definitive evidence of the original implementation language was found.

## Simulation boundary

The executable constructs an object containing the string `A10SIM.DLL` at `0x004195bf`. The routine beginning at `0x00414fe0` calls `LoadLibraryA`, resolves all six exports with `GetProcAddress`, and checks the resulting pointers. The DLL exports these decorated names; the suffixes indicate the byte count of 32-bit stack arguments, not their types:

| Export | DLL address | Preliminary observation |
| --- | --- | --- |
| `_SIM_Free@4` | `0x10013620` | Calls an internal routine with one argument. |
| `_SIM_GetName@4` | `0x10013630` | Uses the low 16 bits of its argument as an offset into a table when initialized. |
| `_SIM_Init@4` | `0x10013670` | Stores a nonzero argument, invokes initialization, and reports whether the argument was nonzero. |
| `_SIM_Malloc@4` | `0x10013690` | Calls an internal routine with one argument; return type needs verification. |
| `_SIM_ProcessEvent@16` | `0x100136a0` | Forwards four arguments to `0x10013510`. |
| `_SIM_Start@4` | `0x100136c0` | Checks initialization state and invokes two internal routines. |

These observations are preliminary decompiler interpretations, not established signatures. In particular, calling conventions, callback structures, ownership, and event meanings require tracing at the call sites. The DLL imports Windows APIs from `KERNEL32.dll` and `USER32.dll`; it is not independently portable as-is.

## Graphics and other platform dependencies

The executable imports `DirectDrawCreate`, `DirectSoundCreate`, and DirectPlay, and has strings naming `CDirectDrawScreen` and `CDirectSound`. A call to `DirectDrawCreate` at `0x0040885c` is followed by indirect interface calls through vtables. Replacing the creation call alone will therefore not replace the graphics API; interface methods and associated surface, display, palette, and input behavior must first be mapped. Determine actual use before designing an SDL3 adapter.

The executable's `.text` is about 296 KiB, while its `.rsrc` is about 15.5 MiB; the large executable is not predominantly machine code. The DLL has about 606 KiB of `.text` and 1.7 MiB of resources. Resources may contain important embedded game data and need separate investigation.

## Reproduce the report

Import each binary into Ghidra and run `ghidra/Report.java` using Script Manager, or use `support/analyzeHeadless` from a Ghidra installation:

```sh
mkdir -p data/ghidra-project
analyzeHeadless data/ghidra-project A10 -import data/disc/A-10Cuba/A10Sim.dll \
  -scriptPath ghidra -postScript Report.java
analyzeHeadless data/ghidra-project A10 -import data/disc/A-10Cuba/A10Cuba.exe \
  -scriptPath ghidra -postScript Report.java
```

Use the full path to `support/analyzeHeadless` if it is not on `PATH`. The report prints selected symbols, cross-references, strings, and decompiled export wrappers to the console. The local Ghidra project lives under ignored `data/`; rerunning imports into the same project requires deleting the existing project or using `-overwrite`. Avoid committing derived game binaries or project databases.

Next, recover the executable's calls through the six simulation pointers and document argument structures and events, then inventory DirectDraw vtable calls and extract resource metadata. These steps will establish which components can be reimplemented or adapted first.
