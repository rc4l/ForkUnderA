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
