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
