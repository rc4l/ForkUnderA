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
- **Menu (`wadsrc/static/menudef.txt`)**: `Option "Fullscreen", "fullscreen", "YesNo"` already
  matches UZDoom's `Fullscreen: Yes/No`. `fullscreen` now means borderless, so the label is
  correct as-is. No change.

## Not in scope (tracked follow-ups)

Post-process gamma (borderless can't use hardware gamma ramps — a 2016-era backport),
`vid_scalemode` sub-native render scaling, and window position/size persistence. See
`docs/borderless-feasibility.md`.
