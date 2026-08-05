# video-scale

Makes fullscreen render at the **desktop resolution, filling the whole screen** (native fill),
instead of rendering the picked "mode" into a corner and leaving the rest black. Also lands a
faithful, tested port of upstream's `r_videoscale` scale math as the foundation for genuine
sub-native render scaling later.

## Why this exists

After borderless-video, fullscreen is a borderless desktop window that never switches the display
mode. But our engine still sized the render target to the *requested resolution* (e.g. 1280×800),
so on a larger desktop the frame rendered into the bottom-left corner and the rest was black.
Upstream (GZDoom/UZDoom) fixed the whole class in `b65b83edb` (2018-06-17) by retiring the
fullscreen resolution grid and always sizing the framebuffer to the window's client/drawable size,
with `vid_scalefactor`/`vid_scalemode` providing *optional* sub-native rendering that upscales to
fill.

## >>> SUPERSEDED-BY-UPSTREAM <<<

**This is a faithful stand-in, meant to be replaced by the upstream commit later.** Every touch
point is marked `[rc4l] video-scale` / `SUPERSEDED-BY-UPSTREAM` in the code so it is easy to find
and lift out. When upstream's render-buffers video backend is ported wholesale, prefer replacing
this unit's callers with upstream's `r_videoscale.cpp` verbatim and deleting the bespoke wiring.

Our 2016-era renderer has no scene-present FBO of its own (the scene and 2D normally draw straight
to the window backbuffer), so rather than porting upstream's whole `FGLRenderBuffers` pipeline we
implement a **small self-contained version our own way**: one offscreen FBO (color texture + depth)
at the render/virtual size, and a single `glBlitFramebuffer(..., GL_LINEAR)` that upscales it to
fill the window each frame. One GPU blit per frame — negligible cost. The default (Native, factor
1.0) skips the FBO entirely and renders straight to the backbuffer, byte-for-byte the old path.

### Per-frame resize reconcile

`MaybeResizeForScale` compares the render size against the client size **every frame** and resizes
when they differ, exactly as upstream's `OpenGLFrameBuffer::Update` does (uzdoom@c3702ae9e).

This used to be gated on a `zx_videoScaleDirty` flag, because the client-size query then went
through `SDL_GL_GetDrawableSize`, an expensive Cocoa/Metal call on macOS. That reasoning died with
the SDL backend -- the Cocoa path reads the view's backing bounds, which is cheap. The gate was also
wrong: any resize that did not raise the flag (a window drag, a fullscreen toggle, an OS-driven
resize) left the render target stale with no way to notice. Comparing the sizes *is* the check, so
the flag was removed rather than left unread.

The scale FBO is active only when scaling is actually requested (render size != client size), so
2D-only frames (menus, the console) render straight to the backbuffer with no extra blit.

(An earlier experiment forced the FBO always-on on macOS to dodge the GL-on-Metal drawable stall for
the 3D scene; it hurt pure-2D frames and was reverted. The deeper macOS GL-on-Metal frame-rate
ceiling is a separate, pre-existing issue for the modern render backend, not this feature.)

### The client-vs-render split

The OS window is the **client** size (the desktop for fullscreen borderless; the requested size for
a window). What the engine renders is the **virtual** size from the scale unit (== `SCREENWIDTH`).
`I_SetMode` sets the framebuffer to the virtual size and stashes the client size for window
creation; the framebuffer builds the scale FBO whenever the two differ and blit-upscales at present.

## The pure unit

`computation/videoscale_compute.{h,cpp}` (+ `_test.cpp`, 100% coverage) is a header-pure, faithful
port of upstream's `vScaleTable` and `v_MinimumToFill` math (Copyright 2017 Magnus Norddahl, Rachael
Alexanderson; BSD-3-Clause / GPL-3.0-or-later). `ComputeScaledViewport(...)` combines upstream's
`ViewportScaledWidth` / `ViewportScaledHeight` / `ViewportPixelAspect` into one pure function that
takes every engine input (scale mode, factor, custom size, crop-aspect, active ratio, min bounds)
as a parameter. No engine/GL/SDL/CVAR includes.

## Files

- `computation/videoscale_compute.{h,cpp,_test.cpp}` — the pure, tested math (scale table +
  `ComputeScaledViewport` + `ComputeScalePresentPlan`). No engine/GL/SDL includes.
- `videoscale.cpp` — the CVAR glue (`vid_scalemode`, `vid_scalefactor`, `vid_scale_custom*`,
  `vid_cropaspect`) + `vid_showcurrentscaling`. Faithful to upstream's `r_videoscale.cpp` surface.
  Listed in `src/CMakeLists.txt` (engine target only — it isn't a `*_compute.cpp`, so it's not in
  the test build).

## In-place engine edits (enumerate every one — features/README.md law)

- `src/sdl/hardware.cpp` — `I_SetMode`: compute the client size (desktop for fullscreen, requested
  size for a window), run `ComputeScalePresentPlan` to get the virtual render size, pass that on as
  width/height, and stash the client size. `EXTERN_CVAR`s + `#include`s the unit.
- `src/sdl/sdlglvideo.cpp` — create the SDL window at the stashed **client** size (not the render
  size); defines the `zx_pendingClientWidth/Height` stash.
- `src/gl/system/gl_framebuffer.{h,cpp}` — the GL executor: `UpdateScaleBuffer` builds/binds the
  scale FBO (color + depth) and makes it the render target; `BlitScaleBuffer` upscales it to the
  client rect before `Swap`; `GetClientSize` reads the drawable. Inactive (Native/1.0) => backbuffer
  path unchanged.
- `src/gl/renderer/gl_renderer.{h,cpp}` — `FGLRenderer::mOutputFB`: `EndOffscreen` restores the
  active screen target (scale FBO or backbuffer) so camera textures don't unbind the scale buffer.
- `wadsrc/static/menudef.txt` — `VideoModeMenu`: "Render scale mode" + "Render scale factor" plus
  the `ScaleModes` OptionValue list.

## Platforms

Both backends: SDL2 (macOS/Linux) and the native Win32 backend do the client/render split and the
shared GL executor (`GetClientSize` reads the drawable / client rect per platform). See
[[windowed-video]] for the Win32 window details.

## Not in scope (tracked follow-ups)
- Per-monitor `vid_adapter` selection for fullscreen; High-DPI (retina) native-pixel rendering via
  `SDL_WINDOW_ALLOW_HIGHDPI` (today we fill at the desktop point size and let the OS upscale).
