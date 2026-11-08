# DirectDraw graphics tracing

These observations use the original executable's preferred image base and the screen object discussed in [interface-tracing.md](interface-tracing.md). Interface method names, offsets, flags, and HRESULT values were checked against the local MinGW `ddraw.h`. The call sites below are static evidence; they have not yet been correlated to individual frames in a debugger.

## Display modes

The initialization path queries `IDirectDraw2` and initially uses `DDSCL_NORMAL`. A separate routine at `0x00408a20` calls `IDirectDraw2::SetCooperativeLevel` at `0x00408a3b` with flags `0x13` (`DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE`). At `0x00408a57`, it calls `SetDisplayMode` with width, height, and bits per pixel read from the selected mode object, and zero refresh rate and flags. Therefore the port needs to account for both initial windowed setup and an exclusive fullscreen mode transition; the native implementation need not reproduce the Windows COM interface itself.

## Surface operations

Several indirect calls in the screen object match the original `IDirectDrawSurface` vtable. Object field offsets here are relative to that screen object, not the surface itself.

| Call site | Operation | Observed behavior |
| --- | --- | --- |
| `0x004088ed` | `Lock` | Uses the surface at `+0x44`, a zeroed 108-byte descriptor, a null rectangle, `DDLOCK_WAIT`, and a null event. |
| `0x004088fa` | `Unlock` | Immediately calls the `+0x44` surface with a null argument; the code subsequently uses a descriptor field unless the lock result is `DDERR_SURFACELOST`. |
| `0x00408999`, `0x004089b6` | `IsLost` | Checks surfaces at `+0x3c` and `+0x40`. |
| `0x004089a9`, `0x004089c6` | `Restore` | Restores the corresponding surface when `IsLost` returns `DDERR_SURFACELOST` (`0x887601c2`). |
| `0x00408a0d` | `BltFast` | Copies from the surface at `+0x40` to the surface at `+0x3c`, with destination `(0, 0)`, a source rectangle at object offset `+0x4c`, and `DDBLTFAST_WAIT`. |

The interface identities for these surface fields are inferred from method offsets, argument counts, and error handling; creation and any `QueryInterface` calls still need to be traced. Do not assume the exact pixel format or surface roles from these calls alone. In particular, `Lock` returns an address and pitch through its descriptor, so an SDL3 replacement will need to preserve the actual pixel memory layout and update semantics, not just create a window.

## Next tracing targets

Trace creation and ownership of the surfaces at offsets `+0x3c`, `+0x40`, and `+0x44`; identify their dimensions, pixel formats, palette behavior, and which routines write pixels. Then instrument one frame in the Wine reference run and compare it with a native test harness before implementing a broader graphics layer.

## Surface creation and palette

The setup routine creates the surface stored at `+0x3c` at `0x004084e3` with `DDSD_CAPS` and capabilities `DDSCAPS_PRIMARYSURFACE | DDSCAPS_VIDEOMEMORY` (`0x4200`). If creation fails, it retries without `DDSCAPS_VIDEOMEMORY` at `0x00408511`. It creates the surface at `+0x40` at `0x004085cc` as `DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY` (`0x4040`), again retrying without the video-memory flag; it fills width and height from the selected display mode. The primary surface receives a palette via `SetPalette` at `0x00408533`. The palette creation call at `0x0040847e` uses `DDPCAPS_8BIT | DDPCAPS_ALLOW256` (`0x44`).

The code tries locking each surface in turn and, on success, stores that surface in `+0x44`, together with values from the returned descriptor. Thus `+0x44` is an alias selected from the earlier surfaces on successful lock, not an independently established third surface. These details describe this creation path and do not prove every runtime mode uses the same pixel format or palette.

## Embedded graphics inventory

Use `tools/pe_resources.py` to enumerate the complete PE resource tree or extract one raw resource into an ignored local directory:

```sh
python3 tools/pe_resources.py data/disc/A-10Cuba/A10Cuba.exe --type DIB > data/dib-inventory.json
python3 tools/pe_resources.py data/disc/A-10Cuba/A10Cuba.exe --type DIB --name BADGES --extract data/badges.bmp
python3 -m unittest discover -s tests -v
```

The tool emits JSON metadata including the file offset, RVA, size, and language; numeric type or name IDs use `#`, for example `--type '#2'`. Extraction requires exactly one match and refuses to overwrite an existing output. It extracts resource bytes unchanged and does not interpret their formats.

For the identified disc build, the executable has 347 resource leaves, including 209 `DIB` and 62 `WAVE` entries. The DLL has 520, including 365 `SPDX`, 134 `NRCT`, 3 `DATA`, and 1 `CLUT`. Sampled `DIB` entries `BADGES` and `AIRBARBEGIN` start with BMP file headers and are uncompressed 8-bit indexed images; `BADGES` is 1128×42 and `AIRBARBEGIN` is 2×17. This does not establish the format of every resource with the same type. Extracted assets remain under ignored `data/` and must not be committed.

The native SDL3 bitmap probe now loads and renders these BMP resources. With SDL 3.4.18 and the software renderer, a rendered snapshot of `BADGES` matched the extracted image pixel for pixel; a synthetic 3×2 indexed image also exercises row padding and palette colors in the CTest suite. The probe uses SDL’s conversion to a texture, and does not yet implement DirectDraw locking, palette updates, or the simulation interface.
