# windowed-video

Retires the fixed resolution grid and makes the windowed window a normal, freely **resizable** OS
window, matching upstream GZDoom/UZDoom (which deleted the grid in `b65b83edb`). Fullscreen is
borderless-desktop and always fills; the windowed size is set by dragging the window edges, by
`vid_setsize`, or in the menu.

## >>> SUPERSEDED-BY-UPSTREAM <<<

A faithful stand-in meant to be replaced by the upstream video backend later. Upstream persists
window geometry in `win_w`/`win_h`/`win_x`/`win_y`/`win_maximized`; we reuse our existing archived
`vid_defwidth`/`vid_defheight` (the same role) and persist size only. Every touch point is marked
`[rc4l] windowed-video`.

## How it works

The heavy lifting is already done by [[video-scale]]: `OpenGLFrameBuffer::MaybeResizeForScale` runs
each frame and resizes the render target in place to match the actual drawable (no window teardown).
So a window resize needs only three things:

1. **Resizable window** — the windowed SDL window is created with `SDL_WINDOW_RESIZABLE`. The render
   target follows the new drawable live; the resize is applied *after* `Unlock()` so reallocating
   the canvas backing store never dangles the locked `Buffer` pointer.
2. **Persist the size** — on `SDL_WINDOWEVENT_SIZE_CHANGED` (drag or `vid_setsize`), the new size is
   stored in `vid_defwidth`/`vid_defheight`, so the window reopens the same.
3. **`vid_setsize`** — faithful to upstream: `vid_setsize <w> <h>` sets a specific size; with no args
   it re-applies `vid_defwidth`/`vid_defheight` (used by the "Apply windowed size" menu command).

## In-place engine edits (enumerate every one — features/README.md law)

- `src/sdl/sdlglvideo.cpp` — windowed window created with `SDL_WINDOW_RESIZABLE`; `SDLGLFB::
  SetWindowSize`; the `vid_setsize` CCMD.
- `src/sdl/sdlglvideo.h` — `SetWindowSize` declaration.
- `src/sdl/i_input.cpp` — persist the window size on `SDL_WINDOWEVENT_SIZE_CHANGED`.
- `wadsrc/static/menudef.txt` — `VideoModeMenu`: the resolution grid (`ScreenResolution res_0..9`,
  "Press ENTER to set mode", test mode) is removed; replaced with the windowed-size fields (Width /
  Height text fields + "Apply windowed size") plus the [[video-scale]] Scale mode/factor controls,
  with labels matching upstream.

## Retired, now unused

The C++ grid machinery in `src/menu/videomenu.cpp` (`ScreenResolution` line item, `BuildModesList`,
the mode iterators) is no longer referenced by any menu descriptor and is dormant. It is left in
place (harmless; `M_RefreshModesList` null-guards the absent items) to keep the diff to the upstream
menu code small until the whole backend is replaced.

## Not in scope (tracked follow-ups)

- Window position / maximized persistence (`win_x`/`win_y`/`win_maximized`).
- Windows (Win32) backend parity.
