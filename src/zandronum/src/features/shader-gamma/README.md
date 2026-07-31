# shader-gamma

Applies `Gamma` / `vid_contrast` / `vid_brightness` in a shader when presenting the scene, instead
of programming the operating system's gamma ramp.

## The bug this fixes

Reported as *"when I alt-tab my brightness/contrast are affecting my desktop"*.

`SDL_SetWindowGammaRamp` (and `SetDeviceGammaRamp` on Windows) do not do what their names suggest —
they program the **display's** lookup table, which is global to the whole screen, not scoped to our
window. We set it whenever gamma changed and restored the original ramp **only in the framebuffer
destructor**, i.e. at exit. So alt-tabbing left the engine's brightness curve applied to the
desktop until the game was closed. A crash left it applied indefinitely.

Doing the correction in a fragment shader over our own scene texture means it can only ever affect
our own pixels. There is nothing to restore on focus loss because nothing global was ever changed.

## How it works

The scene already rendered into an offscreen FBO whenever video-scale was active. That FBO is now
also created at 1:1 — the shader can only correct pixels it can sample, so the scene has to land in
a texture even when no scaling is happening. The present step then draws that texture over the
backbuffer with `present.vp`/`present.fp`, applying the gamma uniforms.

If the present program fails to build (missing lump, driver refusing the shader), `ShaderGammaReady()`
stays false and everything degrades to exactly the previous behaviour: the 1:1 FBO is skipped, the
present falls back to the stretch blit, and `DoSetGamma()` resumes using the hardware ramp. Losing
gamma control is survivable; losing the frame is not.

## Formula, and why it is not upstream's current one

The maths is upstream's, taken at the point it was correct rather than at HEAD:

```
val = rgb * Contrast - (Contrast - 1) * 0.5
val += Brightness * 0.5
val = pow(max(val, 0), InvGamma)
```

- The operand order is upstream's fix in `72491049e0`: brightness is folded in **before** the gamma
  `pow`, not after. Applying it after clipped negative brightness/contrast inappropriately.
- That same commit also **removed** the `max(..., 0)` guard that `a8d1197ea7` had added a month
  earlier. It is kept here, because `pow()` of a negative base with a fractional exponent is
  undefined — and upstream's present-day shader has the guard back
  (`pow(max(val, vec3(0.0)), vec3(InvGamma))`).

Upstream's current `present.fp` additionally does HDR, dithering, saturation, white/black point and
a grayscale-formula selector. Those depend on their postprocess uniform-block system, which we do
not have, and none of them are needed to fix this bug.

## Layout

- `computation/gamma_uniforms_compute.{h,cpp}` (+ `_test.cpp`) — cvar → uniform conversion. The
  clamps live here because they are what stops a bad cvar producing a screen the user cannot see
  well enough to fix; bounds match the old `DoSetGamma()` exactly, and NaN falls back to neutral
  rather than reaching the shader (a NaN uniform blanks the frame).
- `shadergamma.{h,cpp}` — the present program: compile, fullscreen quad, uniforms, draw.
- `../../../wadsrc/static/shaders/glsl/present.{vp,fp}` — the shader lumps.
- In-place engine edits:
  - `gl/system/gl_framebuffer.cpp` — init/shutdown, always-on FBO when the shader is ready,
    `PresentScaleBuffer()`, and the early-out in `DoSetGamma()` that stops the hardware ramp.
  - `gl/system/gl_framebuffer.h` — `PresentScaleBuffer()` declaration.

## Deliberately not ported

Upstream's `FShaderProgram` / `FGLRenderBuffers` / `hw_postprocess` stack. That machinery exists to
host bloom, SSAO, tonemapping and a uniform-block system we have none of, and the seam catalog's
standing lesson is not to adopt upstream's frame-loop infrastructure wholesale. This is one program,
one quad, three uniforms. If bloom or SSAO is ever wanted, `FGLRenderBuffers` is the thing to port
then — 118 upstream commits touch it.

## Provenance

Adapted from `uzdoom@81fd6c819fd5a6b71a946ba6e95cb67a76e4cac7`:
- `aeb7df09de` — introduced the shader-gamma path (`gl_presentshader`, `gl_shaderprogram`)
- `72491049e0` — the brightness-before-pow ordering fix
- `dc27011370` — removal of the hardware ramp (`vid_hwgamma`)

None applied as a cherry-pick: our SDL video lives at `src/sdl/`, not `src/posix/sdl/`, we have no
`src/hwrenderer/`, and every hunk against `gl_framebuffer.cpp` rejects because that file carries our
video-scale and instant-replay work. This is a hand adaptation.

`dc27011370` carries an explicit *"don't cherry-pick this — systems that do not use renderbuffers
will still need this feature"*. That warning is about keeping a hardware-gamma fallback for GL 2.x.
It does not bind us: `ComputeGLContextRequests` is only ever called with `wantCore=true`, so the
2.1 compatibility chain is unreachable — we request core 4.1/4.0/3.3 and fail if none is available.
The fallback we keep is a different one (shader build failure), which is stricter than upstream's.
