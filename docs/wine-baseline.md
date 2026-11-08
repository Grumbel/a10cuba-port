# Wine reference baseline

The disc's original Windows executable reached the main menu and displayed its “Tip of the Day” dialog on Linux in an isolated Wine prefix. This is a reference for port testing, not a native build. The visible menu is confirmed; mission loading, flight, audio, and networking have not been verified.

## Setup and observation

On the test host, Wine 11.0 was a WoW64 build and Xvfb provided a 1024×768, 24-bit X display. A copy of `A-10Cuba/` plus the disc-root `CTL3D32.DLL` was used as the working directory. No installer or registry imports were used for the observed menu. The original extraction was not modified. A screen capture and logs from this local experiment are under ignored `data/wine-run/`; they contain original game imagery and are not distributed here.

With the disc already extracted and Wine installed, run:

```sh
./scripts/run-wine-reference.sh
```

The script creates a writable game copy in `data/wine-reference/game` on first use and uses `data/wine-reference/prefix` as its isolated Wine prefix. It leaves both in place for later runs. It does not configure a CD drive or install the game, and uses Wine's default prefix architecture; the host's WoW64 Wine rejects `WINEARCH=win32`. For a headless session, for example:

```sh
xvfb-run -a -s '-screen 0 1024x768x24' ./scripts/run-wine-reference.sh
```

A virtual display alone does not provide interactive input or visible output; use X tools or a screen capture to inspect it. Wine and Xvfb need local socket access. The test host emitted EGL/DRI3 warnings, and a first prefix update emitted setup and OLE errors; neither prevented the observed menu. A timed run ending with exit 124 means the process was stopped by the timeout, not that startup succeeded or failed. The screenshot and window tree established the menu observation.

Next verify a mission can be loaded, record any disc or registry dependencies, and capture representative graphics and input behavior for comparison with a future SDL3 implementation.
