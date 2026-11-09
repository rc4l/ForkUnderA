# borderless-video

Makes `fullscreen` mean a **borderless window at the desktop resolution** on every platform,
matching upstream GZDoom/UZDoom (`b65b83edb`, 2018-06-17, which removed exclusive fullscreen
outright) and killing the "my 1920x1080 mode is missing" and "a crash left my desktop gamma
broken" failure classes (there is no display mode to switch, so nothing to restore).

## Two window kinds only

`WINDOW_NORMAL` (decorated, movable, caller-sized) and `WINDOW_BORDERLESS_DESKTOP` (undecorated,
covers the display). Exclusive fullscreen is not offered — same as upstream.

## The pure decision unit (the replaceable core)

`computation/displaymode_compute.{h,cpp}` (+ `_test.cpp`, 100% coverage) is the single source of
truth for what the boolean `fullscreen` CVAR means for window construction. It is header-pure (no
SDL/Win32/engine includes), so **it is easy to replace**: when upstream's video backend is
eventually cherry-picked wholesale, swap this unit plus the two thin backend call sites marked
`[rc4l] borderless-video` and nothing else moves. Backends ask this unit; they never branch on the
raw CVAR.

- `WindowKindForFullscreen(bool)` → `WINDOW_NORMAL` / `WINDOW_BORDERLESS_DESKTOP`
- `BorderlessWindowRect(dispX, dispY, dispW, dispH, &x, &y, &w, &h)` — the covering rect (trivial
  today; isolated so multi-monitor / work-area tweaks land here with a test, not in backend glue)

## In-place engine edits (enumerate every one — features/README.md law)

- `src/win32/win32gliface.cpp`
  - `#include` the computation unit.
  - `Win32GLVideo::GoFullscreen` — dropped the exclusive `ChangeDisplaySettingsEx(CDS_FULLSCREEN)`
    path; fullscreen no longer switches the display mode.
  - `Win32GLFrameBuffer::Win32GLFrameBuffer` — capture the monitor's real pixel rect; route the
    window-style choice through `WindowKindForFullscreen`; size the `WS_POPUP` window to the full
    monitor via `BorderlessWindowRect`.

## Platforms already done for free

- **macOS + Linux (SDL2, `src/sdl/sdlglvideo.cpp`)**: already request
  `SDL_WINDOW_FULLSCREEN_DESKTOP` and toggle with `SDL_SetWindowFullscreen` — this *is* the
  borderless-desktop model. No change.
- **Menu (`wadsrc/static/menudef.txt`)**: `fullscreen` is shown as `Display mode: Windowed /
  Borderless fullscreen` (`DisplayModes`) in both Display Options and Set video mode, since
  fullscreen means borderless; the web build keeps `Fullscreen: Yes/No`.

## Not in scope (tracked follow-ups)

Post-process gamma (borderless can't use hardware gamma ramps — a 2016-era backport),
`vid_scalemode` sub-native render scaling, and window position/size persistence. See
`docs/borderless-feasibility.md`.

### Open: fullscreen size and placement can name different monitors (Windows, multi-monitor)

Found by reading the code on a one-monitor machine, so not reproduced. In fullscreen the render
and client size comes from the **primary** monitor (`GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` in
`win32/hardware.cpp` `I_SetMode`), while the window is placed on the monitor `vid_adapter` picks
by its position in `EnumDisplayMonitors` order (`win32/win32gliface.cpp`
`GetDisplayDeviceNameMonitorEnumProc` and the `Win32GLFrameBuffer` constructor). With monitors of
different resolutions the game can render at one monitor's size and cover another, or land on an
unexpected monitor, as Windows does not promise the primary monitor comes first. The likely fix is to
take both from one monitor (`MonitorFromWindow`, or the `vid_adapter` one), with the choice in this
feature's computation unit and a test.
