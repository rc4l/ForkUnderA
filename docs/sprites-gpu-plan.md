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

## Stage 0: stop doing GL work for a renderer that is not drawing (partly done)

Already landed, and it was worth more than every sprite optimisation attempted before it:
`glFinish()` and `SwapBuffers()` were running every frame for an idle GL context. Sunder MAP10's
median frame 0.648 ms -> 0.343, p99 2.016 -> 0.528; MAP20's p99 17.1 -> 7.1.

**One more candidate here turned out to be load-bearing and is the first thing Stage 1 has to
explain.** `gl_RenderState.Apply()` runs once per sprite inside `Draw`, before the standalone path
returns without rasterising. Skipping it saves 6-15% of the sprite clocks and **changes the picture**:
MAP04 differs by 0.2% against a 0.0% floor. Something `RegisterSprite` depends on is settled by
`ApplyShader()` rather than by the state setters that precede it. A GL-free sprite path cannot call
`Apply()` at all, so this dependency has to be found and made explicit either way.

## Stage 1: sever the derivation from GL

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

## Stage 2: build the quads on the GPU

Today the CPU builds six vertices and a `MeshPiece` per sprite and appends them: `RegisterSprite` is
~0.28 ms of MAP16's 0.63 ms of sprite work, the largest single piece.

Instead upload one compact record per actor -- position, frame, scale, light, style -- and expand to
quads in a shader. That removes `RegisterSprite`, `DynAppend`, and the per-frame vertex traffic.

**Verified by:** the picture is unchanged, and `S: Render` falls.
**Risks:** the four blend modes the mesh classifies into have to survive; billboarding moves into the
shader (which is where it belongs, and closes the Stage 1 gap); the back-to-front sort has to be done
GPU-side or replaced.

## Stage 3: cull on the GPU

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
