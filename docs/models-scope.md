# Models and voxels: invisible, and what it takes to fix

## The bug

With `fua_dg_standalone` on -- which is the default now -- **an actor that uses a model is not drawn
at all.**

`GLSprite::Draw` splits on `modelframe`:

```cpp
if (!modelframe) {
    ... RegisterSprite(...)                 // feeds the backend's dynamic mesh
    if (StandaloneActive()) return;         // GL's rasteriser is skipped
    ... GL draw ...
} else {
    gl_RenderModel(this);                   // neither registered nor gated
}
```

`RegisterSprite` is inside the `!modelframe` branch, so a model never reaches the mesh. And
`gl_RenderModel` has no standalone gate -- the whole of `gl/models/` contains no reference to
`StandaloneActive`, `RegisterSprite` or `levelmesh`. So it draws through GL into a back buffer the
Diligent child window covers and nothing presents.

`gl_RenderHUDModel`, reached from `gl_weapon.cpp`, has the same problem: a weapon replaced by a model
disappears.

**Why no test caught it.** Models come from MODELDEF; vanilla Doom has none, and neither Sunder nor
the eon maps appear to use any. Every parity run in this work has been on content with zero models,
so the mode shipped with a subsystem that renders nothing and nothing said so. That is worth holding
onto separately from the fix: *the test content decided what could be found.*

## What a model is, in this engine

Three formats behind one interface -- `FDMDModel`/`FMD2Model`, `FMD3Model`, and `FVoxelModel`, which
turns voxel data into a model at load so voxels ride the same path. Each implements:

```cpp
virtual void RenderFrame(FTexture *skin, int frame, int frame2, double inter, int translation);
```

Two things about that signature shape the port:

1. **Interpolation is on the GPU.** `RenderFrame` sets `gl_RenderState.SetInterpolationFactor(inter)`,
   binds *two* frames' vertex attributes from one buffer via `mVBuf->SetupFrame(off1, off2)`, and the
   shader lerps between them. The CPU never blends anything.
2. **A model has its own transform**, applied as a matrix around the draw, plus per-surface skins --
   an MD3 can carry a different texture per surface.

Neither fits the resident mesh as it stands. `MeshPiece` has no transform: every vertex in the mesh
is already in world space. And the Diligent scene shader has no interpolation factor at all -- zero
references -- because nothing in the static world needs one.

`FModelVertex` is `{x, y, z, u, v}` plus a packed normal, which is the same shape as `FFlatVertex`.
That matters for the option below.

## Option A -- resolve on the CPU, feed the existing mesh

Interpolate the two frames and apply the object-to-world matrix on the CPU, then hand the result to
`DynAppend` as an ordinary dynamic piece with the skin as its material.

- **For it.** No new pipeline, no new shader, no new vertex format. It inherits everything the sprite
  path already has: the back-to-front sort, bindless materials, `CaptureShading` lighting, the
  parity harness. One surface becomes one `MeshPiece`, which is what the mesh is already built for.
- **Against it.** Frame interpolation moves back onto the CPU, and the transform with it -- work the
  GPU is doing for free today. The bill is per visible model per frame, so it is bounded by what is
  actually on screen rather than by what the level contains.
- **Cost is probably fine, and should be measured rather than assumed.** Models are a mod feature and
  usually a handful on screen. If a mod puts two hundred high-poly models in view this is the wrong
  answer, and the way to find out is to count triangles per frame before building it -- the same
  check that killed GPU sprite instancing.

## Option B -- port the model renderer

Give the backend its own model path: a two-frame vertex format, a pipeline that takes an
interpolation factor, a per-model matrix, and skin binding through the existing bindless array.

- **For it.** Keeps interpolation and transformation on the GPU, which is where they belong, and does
  not care how many models a mod loads.
- **Against it.** It is a second draw path with its own PSOs and its own shader, parallel to the
  world and the dynamic stream, for a feature no vanilla content uses.
- **This one is a genuine borrow.** Upstream has already done the separation: `src/common/models/`
  has **zero raw GL calls** across every loader, and `modelrenderer.h` defines an abstract
  `FModelRenderer` of twelve virtuals -- `BeginDrawModel`, `SetInterpolation`, `SetMaterial`,
  `DrawArrays`, `DrawElements`, `CreateVertexBuffer` and a few more. Our tree predates that split;
  `gl/models/gl_models.cpp` still has 33 raw GL calls, in two clusters: a VBO wrapper and the draw
  state around `gl_RenderModel`.

  Unlike sprites -- where upstream had not done the work and there was nothing to take -- here the
  abstraction exists and is small. Backporting `FModelRenderer` and writing a Diligent implementation
  is the shape, and it also buys the loaders they have and we do not (IQM, OBJ, UE1).

## Recommendation

**Option A first**, because it is small, reuses the machinery that already produces pixel-exact
results, and turns an invisible subsystem into a visible one in the least code. Option B is the right
end state and the reason to keep upstream's interface in view, but it is a second render path and
models are not where the frame time is.

Take `gl_RenderHUDModel` in the same change: it is a separate entry point with the same fault, and a
weapon that vanishes is more likely to be noticed than a decoration that does.

## The prerequisite nobody can skip

**There is no test content.** Every map used in this work has zero models, so there is currently no
way to tell a fix from a no-op, and no floor to measure a picture against. Before writing any of the
above, get a WAD that uses MODELDEF and one that uses voxels, and establish the GL-driven reference
shot the same way every other parity item here was established.

Without that, this is unverifiable -- and an unverifiable rendering change is how the last several
wrong conclusions in this work got made.

## Built, 2026-08-24: Option A, and models are on screen

`features/levelmesh/modelmesh.cpp`. The frame is resolved on the CPU and the result goes into the
same dynamic stream sprites and decals already use, so models inherit the back-to-front sort,
bindless materials, `CaptureShading` lighting and the parity harness without any of them being
told about models.

Four pieces, and the interesting part is how little each one is:

1. **The geometry stays readable.** `FModelVertexBuffer::LockVertexBuffer` used to hand back a
   mapped GL range that the loaders wrote into and then dropped -- `FMD3Model::BuildVertexBuffer`
   even calls `UnloadGeometry()` on each surface afterwards. Lock now returns plain memory and
   Unlock uploads it whole, so GL gets exactly what it got before and the data is still there to
   read. Model data is tens of KB; the mesh it feeds is megabytes.
2. **One interception point covers all three formats.** MD3, MD2 and voxels end their RenderFrame
   the same way: set the material, `SetupFrame(off1, off2)`, draw. So each captures and then falls
   through to the GL draw only when GL is still drawing -- the same shape as the sprite branch in
   `GLSprite::Draw`, and gated the same way, on `StandaloneActive` rather than
   `BackendPresenting`, because side by side GL's own window is live and still needs the draw.
   MD2 is not indexed (it calls `glDrawArrays`), so the absence of an index buffer is read as
   sequential numbering rather than as nothing to draw.
3. **The transform comes from where it already exists.** `gl_RenderModel` builds the object-to-world
   matrix into the render state for GL's vertex shader; the capture is bracketed around the frame
   draw there and applies that same matrix on the CPU. The sprite is in scope at that level too,
   which is where the light, translucency and sort position come from.
4. **`fua_models`** reports what the last frame captured, and `fua_mesh_models 0` switches the whole
   thing off. That counter is what made this tractable: the content is dark and the props are
   small, so "is a model in view here" was otherwise a question about eyesight.

### Measured

`wdiex02`, sim frozen, `fua_dg_standalone 0`, nine whodunit wads loaded. Both readings are
Vulkan against GL **in the same pair**, which is the only comparison this map allows -- its own
lighting changes between shots, so comparing two Vulkan frames taken seconds apart measures the
map, not the renderer.

| vantage | models off | models on |
|---|---|---|
| spawn (4 models, 29 surfaces, 2986 triangles) | `mean\|d\|` 8.6 | **6.9** |
| the alley at `-6478,1552` (2 models, 351 triangles) | `mean\|d\|` 4.8 | **4.4** |

Switching the capture on changes 63.5% of the pixels at the spawn, so this is not a subtle
addition -- and it moves TOWARD GL at both vantages, which is the direction that matters. Frame
time is unchanged (median 29.7 ms on, 30.4 ms off, both the tic-locked rate of a frozen sim).

### What is still missing, stated rather than implied

- **The HUD weapon model.** `gl_RenderHUDModel` has the same fault and is NOT fixed. It is not the
  same fix: a HUD model is drawn in view space with its own projection, so appending world-space
  triangles would put the weapon in the room. It needs the HUD layer, not this stream.
- **No normals.** `FModelVertex::SetNormal` is a no-op in this tree -- the loaders read normals and
  throw them away, because GL's model shader never wanted them. So a captured surface has no side,
  the backend skips its dynamic-light facing test the way it does for billboards, and a model lit
  from behind is lit all over. Fixing this means carrying normals through the vertex buffer.
- **Interpolation is now CPU work**, per visible model per frame, which is the trade Option A makes.
  At this content's density it does not show. A mod with two hundred high-poly models in view is
  the case that would, and `fua_models` is how to find out before assuming.

### How it was actually verified, after two bad attempts

Both first attempts measured something other than models, and both looked like evidence.

**A cvar A/B across two shots measures the map.** `wdiex02` changes its own lighting, so toggling
`fua_mesh_models` and taking a second capture seconds later moved 63.5% of the pixels whether or
not models were involved. Reported as "models are a big part of the frame", it was the room getting
brighter.

**A frozen A/B measures nothing at all.** The obvious fix -- pause the sim so the map cannot
change, then toggle -- produced a 0.4% difference and read as "models are not drawn". A paused sim
does not rebuild the dynamic stream, so the toggle never reached the frame being captured. This is
the same trap as measuring dynamic lights under `sim.pause`, one layer further out: anything fed by
the per-frame capture is frozen too.

What settles it is a **matched GL/Vulkan pair at one camera, close enough to a model to identify**
it. Standing at `-5716,394,-184` facing `180` on `wdiex02` puts the TCPDCar md3 across the whole
alley: GL and Vulkan both draw the car, in the same place, with its headlights and plate. That is
not a statistic, and it is the only reason to believe the rest of them.

`fua_models` grew the two lines that made this findable: the world-space bounding box of what the
last frame captured, and the alpha, blend, light, colour and material of the last piece handed over.
The box distinguishes "captured nothing" from "captured it into the wrong place", and the piece line
distinguishes both from "handed over black, or at alpha zero" -- three failures that are identical
from outside and have nothing in common inside. `fua_dg_dynstats` then confirms the far end:
`dynamic: 13 sprites -> 42 draws, 3012 tris` is the models arriving where they are drawn from.
