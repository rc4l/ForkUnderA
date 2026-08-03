# posix/ — the non-Windows platform layer

Mirrors upstream's own layout (`uzdoom@1433bf3f7`, 2014-12-18), deliberately. These are
upstream-authored platform files whose value **is** their byte-similarity to upstream: keeping the
paths identical means a future upstream commit maps onto our tree by a single constant prefix
rewrite (`src/` → `src/zandronum/src/`) instead of needing hand-translation forever.

```
posix/          shared POSIX  — used by BOTH backends
posix/sdl/      SDL2 backend  — Linux
posix/cocoa/    Cocoa backend — macOS (upstream 108dcf122, 2016-09-04)
posix/osx/      macOS bits shared by either backend (IWAD picker, our updater seam)
```

`SYSTEM_SOURCES_DIR` is the shared tier and `SYSTEM_BACKEND_DIR` the selected backend; both are
include roots, because headers such as `gl/system/gl_framebuffer.h` include `"sdlglvideo.h"`
unprefixed and used to resolve through the old flat `sdl/` directory.

## Why this is not under `features/`

`features/README.md` is the rule for **our own** additions. This is the staircase exception that
`gl/` already uses: upstream code, kept at upstream paths, modified in place with tagged comments.
Putting it under `features/cocoa-backend/` would guarantee every future upstream Cocoa commit needs
manual re-pathing — the exact opposite of the point.

## posix/cocoa/ — fork deltas against upstream

The tree was vendored **byte-identical** to `uzdoom@108dcf122` in its own commit, so
`git diff <that-sha> -- posix/cocoa/` is always the exact fork delta. Every intentional divergence
is listed here and tagged `[rc4l]` at the site.

| File:line | Delta | Why |
|---|---|---|
| `sdlglvideo.h` (class head) | `DECLARE_CLASS(SDLGLFB, DFrameBuffer)` restored | our `gl_framebuffer.h` declares `DECLARE_CLASS(OpenGLFrameBuffer, SDLGLFB)`, which needs SDLGLFB registered as a DObject. Upstream had shed this by 2016; ZDoom 2.8pre still requires it |
| `sdlglvideo.h` (class head) | `GetTrueHeight()` restored | `uzdoom@c817979ea` removed it upstream; nine sites here still call it (`gl_renderer.cpp:270`, `gl_framebuffer.cpp:193,218`). Recorded as a deliberate skip |
| `i_video.mm` (IMPLEMENT) | `IMPLEMENT_ABSTRACT_CLASS(SDLGLFB)` added | companion to the above, mirroring `posix/sdl/sdlglvideo.cpp:39` |
| `i_video.mm:53` | `r_swrenderer.h` → `r_nullrenderer.h` | GL-only build |
| `i_video.mm:34` | `gl/system/gl_load.h` → `gl/system/gl_system.h` | `uzdoom@e132fc5ee` (GLEW → GLLoadGen) is a recorded skip; our loader is still GLEW |
| `i_video.mm` (class + impl) | `CocoaFrameBuffer` removed, 306 lines | software framebuffer: a GPfx palette blit through `GL_TEXTURE_RECTANGLE_ARB` and `glBegin/glEnd`, which a core profile would reject anyway |
| `i_video.mm` `CreateFrameBuffer` | renderer branch collapsed | nothing to choose between |
| `i_video.mm` `vid_renderer` | snap-back-to-1 body | matches `posix/sdl/hardware.cpp:88-94` |
| `i_video.mm` `I_CreateRenderer` | `#ifndef NO_GL` / `FNullRenderer` | matches `posix/sdl/hardware.cpp:136-149` |
| `i_video.mm` | `BlitCycles`/`FlipCycles`/`ADD_STAT(blit)` removed | measured only the software framebuffer; would be permanently zero |
| `i_timer.cpp:41` | dropped `basicinlines.h` | no such header in our base |
| `i_timer.cpp` `I_GetTimeFrac` | `double` → `fixed_t`, fixed-point body | our `fixed_t` is a strong 48.16 type; body copied from `posix/i_system.cpp:298-311` rather than converting a double |
| `basictypes.h:37` (engine, not posix) | `ULONG` typedef deferred when `__COREFOUNDATION_CFPLUGINCOM__` | CoreFoundation's COM shim does `typedef UInt32 ULONG`; IOKit's HID plugin API pulls it into `i_joystick.cpp`. Nothing in that TU names `ULONG`, and none of the 151 files that use ours include CoreFoundation |
| `d_gui.h:70-74` (engine, not posix) | `GKM_META = 8`, `GKM_LBUTTON` 8 → 16 | `uzdoom@32af6cb0c`; Cocoa reports Command/Meta as its own modifier. Runtime-only flags, never serialised, and `GKM_LBUTTON` had no users |

## Invariants that are easy to get wrong

- **Client sizes are always in PIXELS.** `GetClientWidth()`/`GetClientHeight()` and
  `zx_pendingClientWidth/Height` are backing pixels; only `setContentSize:` converts to points.
  `vid_hidpi` defaults true, so on a Retina display a points/pixels mix-up renders at quarter size
  in the bottom-left corner rather than failing loudly.
- **A dedicated server must never open an NSApplication run loop.** The `-host` / `SERVER_ONLY`
  path bypasses Cocoa entirely.
