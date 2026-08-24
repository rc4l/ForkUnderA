# Geometry paints over the sky, because our sky is a background and GL's is a surface

On `wdiex02`, standing at `-4523,-468,-490` facing `270,-9` (the spot `sky-and-ship`), Vulkan draws
a tanker-and-skyline billboard across the horizon, on top of the sky portal that belongs there. GL
draws the portal's city.

## What it is not, each ruled out by measurement rather than by argument

The band measured is x `0.59..0.88`, y `0.40..0.60` of the frame.

| | max | mean | r/g/b |
|---|---|---|---|
| GL, final | 217 | 8.54 | 10.8 / 8.2 / 6.7 |
| Vulkan, final | 8.3 | 1.59 | 2.9 / 0.9 / 1.0 |
| Vulkan, raw texel (`fua_dg_lightmode 10`) | 29 | 6.59 | 12.1 / 3.8 / 3.9 |
| Vulkan, vertex light (`fua_dg_lightmode 4`) | 102 | 67.8 | flat, = the 0.27 the piece carries |

- **Not the lighting.** The vertex light is 0.27 and the piece records 0.29.
- **Not the models work.** `fua_mesh_models 0` changes nothing.
- **Not the sky portal.** `fua_dg_skyportal 0` leaves the same rectangle over the placeholder sky.
- **Not bindless.** `fua_dg_bindless 0` changes nothing (0.6% of pixels, all console text).
- **Not mipmaps.** `fua_dg_mips 0` changes nothing -- worth stating because an alpha-tested texture
  at 1900 units is a fair suspect, and it is innocent here.
- **Not a lost alpha channel.** `fua_dg_texalpha XCTANKER` reports `alpha 0 in 62.5%`, matching the
  PNG's own 62.5% transparent pixels exactly, and `isMasked yes`. The cut-out works: the ship's
  silhouette is correct in the Vulkan frame. What is wrong is what shows THROUGH it.
- **Not wrong texture coordinates.** `fua_look` reports the backdrop piece as `material SRISE_BK,
  u 1.062..3.062 (2.00 copies), v 0.000..1.609 (1.61 copies)` -- and 1.609 is exactly the wall's
  824 units over the texture's 512, pegged to the ceiling, which is what GL computes too.

## What it is

Two measurements settle it.

**`fua_surface_mapbake_auto 0`** -- which leaves every wall to GL's capture instead of deriving it
from map data -- makes the band read max 183 / mean 7.86 against GL's 215 / 8.47. With the
derivation off, `fua_look` reports **no baked surface along that ray at all**.

**`gl_noskyboxes 1`** -- which turns GL's own skybox off -- makes GL's band read max 2, a flat blank
sky. So the city GL draws there is the sky PORTAL, and the wall behind it is a wall GL never draws.

So: the map-derived bake has no visibility test, by design -- deriving straight from map data is the
whole point of it. It bakes a wall GL's clipper rejects. Everything about that wall is correct: its
place, its texture, its coordinates, its light. It is simply not supposed to be on screen, and the
one place where "not supposed to be on screen" has no other defence is the sky.

**Not reachability.** The obvious cheap rule -- cull sectors not connected to the player through
two-sided lines -- was built, measured and removed: `fua_reach` reported the offending sector 5804
as reachable (3832 of 5993 sectors are), so the rule does not cover this case, and shipping a cull
that drops 2161 sectors on an unproven benefit trades a visible bug for an invisible one.

## The actual difference, and the fix that follows from it

GL draws the sky as a **surface**: `gl_sky.cpp` builds sky geometry from the sky-flagged planes and
lines in view, and the sky is painted through that stencil. Anything further away than a sky surface
is behind it, and the depth buffer says so.

This backend draws the sky as a **background**: full-screen, depth off, before the world, and then
the world paints over it. Those are equivalent only as long as nothing is ever drawn beyond a sky
surface -- which is exactly the guarantee GL's visible-set walk provides and the map bake does not.

So the fix is to make the sky a surface here too:

1. **Capture GL's sky geometry.** GL already builds it -- `RENDERWALL_SKY*` walls and `GLFlat`'s sky
   planes exist to be stencilled, and `RegisterFlat` returns early on `skyflatnum` today precisely
   because GL makes no ordinary surface for them. Take those quads into the mesh as pieces flagged
   `sky` instead of dropping them.
2. **Render the sky into an offscreen image** once per frame -- the portal already does exactly this
   into `g_skyPortalColor`; the dome case renders into the same target instead of straight to the
   screen, so both kinds arrive as one screen-space image.
3. **Draw the sky pieces after the opaque world**, depth test LESS and depth write on, with a
   pipeline whose fragment shader samples that image at `gl_FragCoord.xy / screenSize`. A skybox is
   at infinity, so screen position IS its texture coordinate, and no new UV work is needed.

That deletes the sky-as-background pass rather than adding to it, and it makes "behind the sky" mean
what it means in GL. It also fixes the same bleed for the dome sky, which has it too and where it is
merely less obvious.

**Cost:** one pass over a handful of sky quads, and one offscreen image that already exists for the
portal.

**The trap to avoid:** sky pieces must NOT go in the sorted blend pass with the sprites -- that pass
has depth writes off for good reasons (see `DrawBlended`), and the whole point here is depth.
`DrawSolidDyn` is the shape to copy.

## Until then

`fua_surface_mapbake_auto 0` hides it, at the cost of the map bake -- which is the feature the
derivation exists to provide, so this is a diagnostic and not a setting to ship.

## Where the hook goes, and the one thing that complicates it

`GLWall::PutWall` classifies by `passflag[type]`, and the sky types are `4` -- "special". Those walls
skip `RecordPiece` entirely and fall into the portal branch, which is why no sky geometry is in the
mesh today. That branch is the capture point: GL has already built the quad, with its `glseg` and
`ztop`/`zbottom`, by the time it gets there.

**The complication is `fua_dg_standalone`.** In standalone the level is baked once and GL stops
deriving the scene, so there are no per-frame GLWall objects to capture -- and standalone is the
mode the backend is meant to end up in. So capturing GL's sky quads fixes the embedded and
side-by-side cases and leaves standalone with the bleed.

Doing it properly therefore means DERIVING sky surfaces from the map, the same way walls are derived:
for each seg whose front or back sector has `skyflatnum` on the plane, the quad from that plane
upward (or downward), which is what `GLWall::SkyPlane`, `SkyTop` and `SkyBottom` compute. Their rules
are fiddly -- `SkyTop` alone handles the both-sides-sky case, the sky-hack case and the bordering
case -- but they are rules, not guesses, and `features/surfaces/` is where the equivalents already
live for the ordinary parts.

Two stages, then, and the first is worth having on its own:

1. Capture GL's sky quads at the `passflag == 4` branch, flag them `sky`, draw them after the world.
   Fixes the embedded and side-by-side cases; measurable immediately against this spot.
2. Derive them in `features/surfaces/` beside the wall parts, which covers standalone and is what
   makes the sky-as-background pass deletable.

## Stage 1 was tried and does not work: the wall cache eats it

Built, measured, reverted. `RegisterSkyQuad` at both sky cases in `PutWall`, a `sky` flag on
`MeshPiece`, a `DrawSkySurfaces` pass with its own pipeline sampling the sky image by
`gl_FragCoord`. On the reported spot it captured **zero quads**, and the band did not move.

The reason is `gl_bsp.cpp`:

```cpp
if (zx::levelmesh::TryReplay(segidx, elig, stamp))  done = true;
else { BeginCapture(segidx); wall.Process(...); EndCapture(...); }
```

A replayed seg never runs `GLWall::Process`, so it never calls `SkyNormal`/`SkyTop`, so no sky quad
is ever built for it. That is the wall cache doing exactly its job -- skipping GL's derivation is the
whole point of it -- and it means "capture what GL builds" only ever sees the segs GL still derives,
which on a walked level is the minority.

One detail worth keeping from the attempt: **a map with a sky portal never produces
`RENDERWALL_SKY`**. `SkyPlane` picks `RENDERWALL_SKYBOX` whenever the sector names a skybox, so
hooking `RENDERWALL_SKY` alone captures nothing on exactly the maps that have this bug.

So the two stages collapse into one, and it is the second: **derive sky surfaces from the map** in
`features/surfaces/`, beside the wall parts that are already derived there, from the rules in
`GLWall::SkyPlane`, `SkyTop` and `SkyBottom`. That is the only version that works with the cache and
in standalone, which are the two modes the backend is meant to run in. The backend half -- the sky
image, the pass, the screen-space sampler -- is small and was written in an hour; the derivation is
the work.
