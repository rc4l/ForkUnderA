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
| _(none yet — Phase 1 is the pristine baseline)_ | | |

## Invariants that are easy to get wrong

- **Client sizes are always in PIXELS.** `GetClientWidth()`/`GetClientHeight()` and
  `zx_pendingClientWidth/Height` are backing pixels; only `setContentSize:` converts to points.
  `vid_hidpi` defaults true, so on a Retina display a points/pixels mix-up renders at quarter size
  in the bottom-left corner rather than failing loudly.
- **A dedicated server must never open an NSApplication run loop.** The `-host` / `SERVER_ONLY`
  path bypasses Cocoa entirely.
