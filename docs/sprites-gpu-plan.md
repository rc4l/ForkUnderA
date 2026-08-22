# Sprites, GPU-driven: the plan

The decision, and the work broken into stages that each stand on their own.

## Why this and not baked visibility

Both were on the table. Baked visibility -- `visible[viewSector][actorSector]`, one bit, built when the
level is baked -- is the smaller bet and would make sprite culling affordable, which nothing else has
(see `occlusion-scope.md`: a drawn sprite costs ~0.2 us, so a per-actor test of tens of nanoseconds
cannot win, and only a single array lookup fits the budget).

It was not chosen, for one reason: it fixes performance and nothing else. The port still has to get
off GL for macOS, and sprites are what keeps GL running. GPU-driven sprites is the only path where
those are the same piece of work rather than two. Baked visibility stays available -- if Stage 3
turns out to be the hard part, its culling half can be swapped for a lookup table without disturbing
Stages 1 and 2.

## What the code actually is, measured rather than assumed

The reason to believe this is tractable:

| | |
|---|---|
| `GLSprite::Process` | 383 lines, **18 GL references**, 4 distinct `GLRenderer` fields |
| `GLSprite::Draw` before the standalone bail | ~190 lines: lighting and render state |
| `GLSprite::Draw` after it | ~44 lines of rasterisation, already skipped |
| `gl_SetColor`, `gl_SetFog`, `gl_SetDynSpriteLight`, `gl_FakeFlat` | **zero raw GL calls each** |
| `FRenderState` (the whole file) | **4 raw GL calls**, all inside `Apply()` / `ApplyColorMask()` |

So the lighting pipeline the mesh depends on is already pure CPU computation. GL enters at exactly one
place -- `Apply()` -- and everything above it is engine logic living in files named `gl_*`.

The four `GLRenderer` fields `Process` needs are `mViewActor`, `mViewVector`, `mCurrentPortal` and
`gl_spriteindex`. That is a view context struct, not a rewrite.

## Status

| stage | |
|---|---|
| 0 -- stop doing GL work for an idle renderer | **done** |
| 1 -- sever the derivation from GL | **done** |
| 2 -- build the quads on the GPU | **blocked, see below** |
| 3 -- cull on the GPU | not started, depends on 2 |

## Stage 0: stop doing GL work for a renderer that is not drawing (DONE)

Already landed, and it was worth more than every sprite optimisation attempted before it:
`glFinish()` and `SwapBuffers()` were running every frame for an idle GL context. Sunder MAP10's
median frame 0.648 ms -> 0.343, p99 2.016 -> 0.528; MAP20's p99 17.1 -> 7.1.

Two more landed with it:

- **`gl_RenderState.Apply()` per sprite.** It runs inside `Draw` before the standalone path returns
  without rasterising, pushing CPU-side state into an API that is not about to be used. Worth 11% of
  the sprite clocks and 14% of the frame on Sunder MAP16 (`All` 0.826 ms -> 0.712).

  Recorded here first as load-bearing and reverted, on a single 0.2% picture difference on MAP04
  against one 0.0% floor reading. That was the map's own variance: three loads per config on MAP10
  and MAP04 give 0.0% in all nine comparisons. One reading is not a floor, and this document said so
  two sections further down while I was ignoring it.

- **The GL instant-replay capture was recording a dead buffer.** There are two replay captures: this
  one reads GL's back buffer, and `DrawSceneOnce` copies the Diligent swapchain just before Present.
  With the backend carrying the frame, GL's back buffer is not the presented image -- a screenshot of
  it shows the HUD over grey slabs where the world should be. So it paid a `glFinish`, a full-screen
  `glReadPixels` and a buffer map every capture frame to record a corrupted picture. That was the
  whole of the frame-time TAIL: `Finish` p95 3.078 ms -> 0.000, MAP20's `All` p95 8.65 -> 7.02.

  `WantsFrame()` is still called unconditionally because it owns the recording session's lifecycle --
  short-circuiting past it would stop the Vulkan stream, which is the one that works. And `fua_clip`
  now skips a stream that has had no frames, instead of writing a second file of nothing beside the
  real one and announcing both.

## Stage 1: sever the derivation from GL (DONE)

Move sprite derivation to a backend-neutral module the way `hud2d` and `features/surfaces` are, so it
compiles and runs without a GL context.

- Replace the four `GLRenderer` fields with an explicit view context passed in.
- Resolve the `Apply()` dependency above: identify what it settles, and set it explicitly.
- Keep `CaptureShading` as the single source of lighting truth -- it is already pure computation, and
  a second implementation of every render style is the thing this port has refused to build.

**Verified by:** the picture is unchanged, and the sprite path builds with GL excluded.
**Expected performance change:** none. This stage buys the macOS port, not milliseconds.

**Known gap this stage should close.** GL draws the billboard-rotated vertices (`v1..v4`);
`RegisterSprite` reads the un-rotated `x1/x2/y1/y2/z1/z2`. So `RF_ROLLSPRITE`, `RF_FORCEXYBILLBOARD`
and `gl_billboard_mode 1` are ignored by the mesh today. Latent with stock settings -- default
billboard mode is 0 -- and wrong for any mod that uses them.

## Stage 2: build the quads on the GPU -- BLOCKED, and here is the wall

The prize is real and was measured: `RegisterSprite` is ~0.28 ms of MAP16's sprite work, and the
backend's own `BuildDynamic` -- which no `stat rendertimes` clock covers -- is another 0.43 ms. Both
exist only to turn four corners into six vertices twice over.

What blocks it is the pass those sprites are drawn in, and the constraint is already written into the
code: **every sprite goes through one back-to-front pass with depth writes OFF.** The comment at the
dynamic draw loop records what happened when that was last challenged -- splitting the opaque sprites
into a depth-writing pass let an alpha-tested impact sprite occlude the additive glow behind it,
"black holes where a plasma burst's bright core should be".

That matters because 98% of sprites are opaque (Sunder MAP16: 2985 of 3053; MAP10: all 1406), and an
unordered instanced draw of the opaque ones is the obvious GPU-driven shape. It is exactly the shape
that was tried and reverted.

So GPU expansion has to keep one draw per sprite in the existing sorted order, which means a second
set of pipelines with no vertex input -- the sprite draws pick between six PSOs already
(trans/add x plain/decal/redAlpha) and each would need a GPU-expanded twin. That is a large change to
a renderer that currently matches GL pixel-for-pixel on every map that repeats, for ~0.4 ms on a
frame that is now 0.71.

**Not attempted, deliberately.** The next person should decide whether the pass structure can be
revisited at all before duplicating six pipelines around it -- and if it can, the win is much larger
than 0.4 ms, because unordered instancing collapses 3053 draws into one.

## Stage 3: cull on the GPU -- not started, depends on Stage 2

Build a depth pyramid over the world the backend has already drawn, and reject actors against it in
the same pass that expands them. This is the culling that could never pay on the CPU.

**Verified by:** the picture is unchanged, and the counts agree with the CPU occluder that was built
and measured for exactly this purpose -- `git show 10b5015a` restores it, and it is pixel-exact on
nine maps.
**Risks:** same-frame culling needs a readback, so use the previous frame's pyramid and accept one
frame of latency; pad the test box so a sprite stepping out from behind a corner does not pop.

## How each stage is judged

The same way everything else in this port has been: an alternating A/B inside one instance, minimum
and p10 of many samples, and a picture diff against the map's own reload floor.

**Do not use Sunder MAP16 for picture parity.** It has a bistable element -- the same 1.7% at row 810
has now appeared for four unrelated toggles, and its cross-config reading has been 0.1%, 0.7%, 0.8%
and 1.7% on the same build. MAP10, MAP04 and dbab01-05 repeat within 0.0-0.1% and are the maps to
judge on. MAP16 stays useful for *timing*, where it is the heaviest case available.
