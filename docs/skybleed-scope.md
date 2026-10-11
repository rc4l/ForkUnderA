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

## The corrected conclusion (2026-08-24, second pass)

Two more attempts failed, and the measurements that killed them are worth more than the code was.

**Sky surfaces do not fix it, and the reason is geometric.** Derived sky quads were built the way
`GLWall::SkyNormal`/`SkyTop` build them, drawn after the world with depth on. First cut emitted one
per seg of every sky sector and was far worse than the bug -- the horizon band went from mean 1.6 to
36.7 against GL's 7.3, because a 32768-tall quad at every sky seg is an invisible wall across every
open area. Corrected to GL's real rule (emit only where the sky does not continue into the back
sector) it changed nothing at all.

`fua_sky_log` says why: GL emits **376 sky quads from this vantage, every one of them z 320..32768**.
From an eye at z -441 that is 22-32 degrees above the horizon at these distances -- rows 60..130 of
640. The band in question is rows 256..384. **No sky surface covers it, in either renderer.** So the
sky-as-a-surface difference is real and worth fixing one day, and it is not this bug.

**And the wall is not lit wrongly either.** Standing 340 units from it, facing it, GL and Vulkan
agree to within noise: mean 3.10 against 3.06, max 4.7 both, 1.4% of pixels differing. Vulkan draws
that wall correctly. It draws it *dark*, and so does GL.

So what is left is exactly what the bisection said and nothing more: **at 1900 units GL does not draw
that wall and Vulkan does.** GL's clipper rejects it; the map-derived bake has no visibility test and
produces it; and the sky is where a wall nobody should see has nothing to hide behind. The bright
city GL paints in that band is its sky portal showing through a region Vulkan has filled with
geometry.

**That makes the fix occlusion culling, not sky work** -- a visible-set test for the derived bake,
which is a real feature (a PVS, or a clipper walk at bake time) and not a patch. Everything short of
it that has been tried here -- reachability, sky capture, sky derivation -- fails for the same
reason: they each answer a question that is not "can this surface be seen from where the player is".

Nothing from the second pass is in the tree. What is kept is this section, `fua_look`'s material and
u/v reporting, and `fua_dg_texalpha`.

## Fixed (2026-08-24): the sky is a surface, taken from GL

Three derivations of the sky quads were built and all three over-emitted -- the last one by a factor
of hundreds, because GL emits sky quads only for segs its BSP walk reaches. On the reported vantage
GL produces them for **four linedefs**; the map has thousands of sky segs. Deriving them from map
data cannot know which four without redoing the visibility, so the quads are **taken from GL** at
`GLWall::PutWall`, where they already exist, and they carry GL's visibility with them.

Four things had to be true, and each one failed silently on its own:

1. **Both sky cases must be hooked.** A map with a sky portal never produces `RENDERWALL_SKY` --
   `SkyPlane` picks `RENDERWALL_SKYBOX` whenever the sector names a skybox -- so hooking the obvious
   one captures nothing on exactly the maps that have this bug.
2. **The wall cache must not replay those segs.** A seg `TryReplay` accepts never runs
   `GLWall::Process`, so it never builds a sky quad. Sky-bordering segs are now always processed:
   four linedefs in view, a few hundred in the level.
3. **The quads need a list of their own.** GL re-enters its scene builder for its own skybox portal,
   and that path calls `DynClear`, so anything the wall walk put in the shared dynamic stream is
   gone before the backend reads it -- 5911 quads captured, 0 runs built. The list now resets on the
   first push AFTER a read, which needs no agreement between GL and the backend about when a frame
   starts. Every explicit clear was wrong in one direction or the other.
4. **The depth test must be LESS_EQUAL.** A sky surface is coplanar with the wall it replaces -- GL
   draws sky INSTEAD of that wall, not in front of it -- so under the default LESS it fails every
   time and the pass draws nothing while looking perfectly healthy: quads captured, draws submitted,
   no pixels.

Plus one that is geometry rather than plumbing: **a sky surface may not reach below the horizon**,
which sits at eye level. Baked quads cannot know the eye height, so the vertex shader clamps to
`uCameraPos.y`. Without it the quad paints sky across the water in front of the far shore -- the
white streak, mean 44 against GL's 1.8 on that row.

### Measured, at the vantage this was reported from

| | GL | Vulkan before | Vulkan after |
|---|---|---|---|
| horizon band, peak | 204 | 8.3 | **204** |
| horizon band, mean | 8.09 | 1.59 | **7.58** |
| streak row, mean | 1.47 | 30.7 | **7.18** |

The rectangle over the horizon is gone and the skyline spans the frame as it does in GL.

### Not clean yet, and stated rather than buried

Two other vantages moved the wrong way: the alley at `-6478,1552` from 15.1% of pixels differing to
20.1%, and the spawn from 37.8% to 40.8% (their `mean|d|` from 4.4 to 5.3 and 8.6 to 10.6). The car
vantage improved slightly, 9.6% to 9.4%. The sky surfaces are painting sky in places GL does not,
and since the quad set is GL's own the difference has to be in how it is drawn -- GL paints its sky
through a stencil with the depth cleared, this pass draws the quads directly. `fua_mesh_sky 0`
switches the whole thing off.

## The horizon cut (2026-08-24, final)

One more trap worth writing down because it looked like a fix and was a tautology: clamping the sky
vertex to eye level and THEN testing the clamped height in the fragment shader can never discard
anything -- the test is comparing a value against the bound it was just forced into. It changed the
picture by 0.6 of a level and read as "the pass is innocent".

The quad now keeps its real height and the FRAGMENT below eye level is discarded, which cuts the sky
exactly at the horizon instead of near it. Measured at `-4523,-468,-490` facing `270,0`, the rows
just below the horizon go from Vulkan 5.4 against GL 1.2, to **19.16 against 19.51** -- the two
renderers agreeing to within noise -- while the band above stays matched at 27.52 against 25.73.

**Removed 2026-10-10.** GL fills a sky wall through its stencil below eye level too, and with the quads taken
from GL the cut only let level geometry show through that part of the wall (WDI18, queue #21); this vantage
still matches GL without it, `mean|d|` 0.25 against 0.24 with it.

### Where it stands

| vantage | before this work | after |
|---|---|---|
| the reported sky vantage | 8.3 peak in the horizon band against GL's 204 | matched, frame `mean\|d\|` 4.4 |
| the car | 9.6% of pixels, `mean\|d\|` 3.2 | **4.8%, 2.7** |
| the alley at `-6478,1552` | 15.1%, 4.4 | 19.4%, 6.1 |
| the spawn | 37.8%, 8.6 | 40.7%, 10.6 |

The two that got worse both have their worst row at the TOP of the frame, which is the sky: a sky
quad coplanar with a wall wins the LESS_EQUAL tie and paints over it. LESS_EQUAL is what the tanker
wall needs (there GL draws sky INSTEAD of the wall) and what the alley wall does not (there GL draws
the wall). The two cases are identical to a depth test and differ only in whether GL considers the
wall visible -- which is the same visibility question this whole document keeps arriving at.

`fua_mesh_sky 0` turns the sky surfaces off; the full-screen background is painted either way (see below).

## The band under the skyline, and where it actually lives

The blue rectangle under the city is NOT the sky pass reaching below the horizon -- that was fixed
and measured (rows just below the horizon: Vulkan 19.16 against GL 19.51). Probing rc4l's exact
camera, the rectangle is the sky quad for linedef 30451 painting the SKY IMAGE, and the band is in
that image: the portal render has a bright neutral stripe at rows 0.58-0.61 with black beneath it.

Ruled out: it is not the room painting a sky of its own. A guard was added for that -- a skybox room
must not draw a sky, since the room IS the sky and anything it paints lands in the image the sky is
sampled from, which is the same recursion GL blocks with `MF_JUSTHIT`. The guard is right and it
moved the band by 1.6 of a level, so the stripe is the room's own geometry: a lit surface at the
skybox room's horizon.

Which leaves the question this document keeps arriving at. GL renders the same room and gets the
same stripe in its portal, and does not show it -- because GL's stencil is the sky quad's VISIBLE
extent, trimmed by the world geometry in front of it, while this pass draws the whole quad. Trimming
it needs the visible set.

`fua_mesh_sky 0` switches the sky surfaces off.

## The rectangle is the PORTAL IMAGE, and it predates all of this

Measured with `fua_mesh_sky 0` -- the sky-surface work switched off, i.e. the old full-screen blit --
at two window sizes, sampling the sky band at the left edge, the centre and the right edge:

| | left | centre | right |
|---|---|---|---|
| GL, 1024x640 | 18.87 | 15.18 | 6.43 |
| Vulkan, 1024x640 | **1.48** | 14.52 | **1.94** |
| Vulkan, 1280x720 | dark | city | dark |

The sky portal has only ever covered a **central rectangle of the screen**, at every resolution. It
was never visible in the numbers here because every band measured in this document was taken between
x 0.59 and 0.88 -- inside the rectangle. rc4l saw it immediately because a person looks at the whole
window.

That reframes everything above. The horizon bleed, the white streak and the blue quad were all
measured and fixed inside the rectangle, and they were real; but the reason the sky looks wrong from
a normal viewing position is this, and it has nothing to do with sky surfaces, depth tests,
horizons or visibility.

**The blit is a full-screen triangle sampling the whole texture, so the texture itself has content
only in the middle: the portal RENDER fills only part of its target.** The corners are the clear
colour, which means nothing rasterised there -- and inside a sealed skybox room every direction
should hit a wall. So the likely cause is that the skybox room's own geometry is largely NOT IN THE
MESH: the room is an area GL visits only while rendering the portal, and what the portal draws is
whatever happens to be baked, which here is the city billboard in the middle and little else.

**Next step, and it is a cheap one:** count what the portal's DrawWorld actually draws (batches and
triangles) compared with the main pass, and check whether the skybox room's sectors have baked
geometry at all. If they do not, the fix is in the bake, not the backend.

`fua_mesh_sky` now defaults to OFF: the sky surfaces are right where they were measured and this
larger fault sits underneath them, so shipping them on would only change which wrong picture appears.

## The cause, proved: the derived bake fills the SKYBOX ROOM with the whole level

One measurement settles it, and it is reproducible in two commands. Sampling the sky band at the
left edge, the centre and the right edge of the frame, GL against Vulkan:

| | left | centre | right |
|---|---|---|---|
| GL | 18.57 | 17.19 | 8.48 |
| Vulkan, `fua_surface_mapbake_auto 1` (default) | **1.96** | 14.8 | **2.25** |
| Vulkan, `fua_surface_mapbake_auto 0` | **17.51** | 16.30 | **9.39** |

With the map-derived bake off the sky matches GL across the whole width. With it on, the sky exists
only in the middle of the window.

The reason is the portal camera. A sky portal renders the world from an `ASkyViewpoint` that stands
in a sealed room off the side of the map. GL walks the BSP from there and reaches only the room. The
derived bake has no walk: it puts the ENTIRE LEVEL in one mesh, and the portal pass then draws the
entire level from inside that room -- the map's own walls standing all around the portal camera,
dark, with the room's city visible only where it happens to be nearer. That dark surround IS the
rectangle, and it is why the sky looks like a picture hung on a wall.

It also explains why every measurement in this document missed it: the bands sampled here run from
x 0.59 to 0.88, inside the rectangle. A range query looks where it is told.

### The fix, and what went wrong with the first attempt

The two passes need two halves of the mesh: the portal draws the skybox room, the main view draws
everything else. The room is a flood fill from the viewpoint's sector through two-sided lines --
built, and it works: `fua_skyroom` reported "viewpoint sector 3690, 27 sectors, sealed skybox room".

Splitting the batches on that flag was tried and reverted: the main view lost geometry it should
have kept and the portal came back blown out (the sky band read 167 against GL's 16.8, and the frame
went to 41% of pixels differing). The flag reaches the pieces through `MeshSetCurrentSector`, which
the bake sets per seg -- the capture path does not set it at all, so pieces GL contributes are
stamped with whatever sector the last derived seg happened to leave behind. That is the thing to fix
before trying the split again.

**Until then `fua_surface_mapbake_auto 0` gives a correct sky**, at the cost of the map bake.

## Fixed, 2026-08-24: the portal draws its own room, and the sky is a surface

Two changes, and neither is the one this document spent most of its length on.

**1. The portal draws only what stands near ITS OWN camera** (`fua_dg_skyportal_reach`, 2048 units).
The sky portal renders from a viewpoint inside a sealed room off the side of the map; GL walks the
BSP from there and reaches only the room, while the map-derived bake has no walk and put the whole
level in the mesh -- so the portal drew the map from inside the room, its walls standing all around
that camera. A reach test rather than a sector flood, because a batch already carries its bounds.
2048 measured best of 2048/4096/8192: `mean|d|` 5.1 against 7.0, the wider two letting map geometry
back in.

The bug that made this look impossible for an hour: the camera position was recorded AFTER the
portal restores `viewx`, so the test measured from the PLAYER and excluded the room along with
everything else -- every reach value drew nothing, which reads exactly like the test being wrong in
principle.

**2. The sky is drawn as SURFACES that cut at the horizon** (`fua_mesh_sky`, on). That is what
removes the bright streak along the waterline: the full-screen background blit paints the portal
image everywhere including below the horizon, where the portal holds its own room edge.

### Measured, at rc4l's camera, 1280x720

| | GL | Vulkan before | Vulkan after |
|---|---|---|---|
| sky, left edge | 17.23 | 1.96 | **16.73** |
| sky, centre | 22.57 | 14.8 | **21.09** |
| sky, right edge | 8.01 | 2.25 | **7.67** |
| streak row | 1.28 | 30.7 | **2.98** |

Stable across a wiggle: `mean|d|` 5.1 at yaw 290 and 5.1 at 294. Other vantages: the car improves
from 9.6% of pixels differing to 7.7%, the alley sits at 19.8% against 15.1% before the sky work,
the spawn 39.5% against 37.8%. Those two remain worse and remain unexplained; their worst rows are
at the top of the frame, which is sky.

## What is left: the residual band, measured

At `-4435,-502,-486` facing `310,2.8` (the spot `sky-band-now`), the box just under the skyline --
x 0.36..0.79, y 0.462..0.496 of the frame:

| | mean | tint |
|---|---|---|
| GL | 0.70 | neutral, essentially black |
| Vulkan, `fua_mesh_sky 0` (background blit) | 33.75 | neutral -- the old bright streak |
| Vulkan, `fua_mesh_sky 1` (sky surfaces) | **13.85** | blue, `b-r` +8.4 |
| Vulkan, `fua_dg_skyportal 0` | 162.80 | the placeholder sky, as expected |

So the sky surfaces more than halve it and do not remove it. The band sits BELOW the horizon, and the
pass already discards fragments below eye level (`vUp = aPos.y - uCameraPos.y`, unclamped, discarded
under -0.5), so whatever paints it is not doing so through the sky quads' own geometry.

A diagnostic worth not repeating: replacing the fragment colour with a constant to see the pass's
coverage DISABLES the pass -- with `uSky` unused the compiler drops it, the static variable lookup
fails, and the pipeline releases itself with "sky surfaces off: pipeline missing its texture". The
frame then shows the sky region unpainted, in the clear colour, which looks like a result and is an
artefact of the diagnostic.

What that accident did show is worth keeping: the unpainted sky region is a bounded rectangle, so the
world geometry around it is being drawn where GL has sky. That is the same visibility question this
document has arrived at four times now, in a smaller form.

## The cause, found (2026-10-09): the lines are `Line_Horizon`, and nothing here knew

The four lines that bound the harbour at rc4l's vantage carry special 9, `Line_Horizon`.
`fua_sky_log` gives it away: half of GL's sky quads there run from the EYE to the ceiling (`skyq -445
320`), which is `DoHorizon`, not `SkyNormal`. GL never draws a horizon line as a wall. Above eye level
it paints the sky, and below it a horizon portal carries the floor out to infinity. The map-derived
bake knew none of this, so it built three of them as one-sided walls of their `SRISE_BK` middle
texture. That slab is the dark block across the skyline. With nothing drawn below eye level, the
clear colour (0.05, 0.06, 0.09) showed through; that is the band under the skyline and its blue tint.

wdiex02 has 102 horizon windows, not four. Fixed in `features/horizon/` -- see its README: no wall
parts on a horizon line, and each line becomes a window that writes depth and is painted with the
sky above the eye and a ray-cast floor plane below it.

Measured at `sky-bleed-rc4l` (16-pixel block means, Vulkan against GL):

| box | before | after |
|---|---|---|
| under the skyline | 10.78 | **0.48** |
| water | 0.35 | 0.39 |
| sky, right (the slab) | Vulkan 2.6 against GL 7.6 brightness | 7.4 against 7.9 |

Standalone mode draws the same picture (block difference to the walked frame 0.1 to 1.7). The pass
costs about 0.02 ms at p50 with per-pass culling (0.12 ms before the culling went in).

Also fixed on the way: `RegisterSkyQuad` read `wall.skybox` out of a union whose live member on a
plain sky is `wall.sky`, so every map whose sky is a texture crashed under Vulkan -- Doom 2 MAP01 on
load. wdiex02 never hit it because every sky there is a skybox. `CaptureShading` also restores the GL
colour and fog state it writes now, since its callers run inside GL's frame -- hygiene, not a fix for
the black frames below.

### Still open: the skyline is squashed

The city in the sky stands about 0.83 times GL's height (spire 145 px above the horizon against 174
at pitch 0). It is NOT the portal: standing inside the skybox room the same squash shows in the
plain view, on the room's `SRISE_FT` wall (linedef 3, a 1024x512 PNG on a 512-unit wall, no sidedef
scale). The derived piece maps it v 0..1, which is what the rules say; GL draws it taller. The tops
move by 0.83 and the bases by about 0.94, so it is not a single scale either. This is the derived-wall
alignment question again, and it is the next thing to measure rather than guess.

### Capture caveats found while building the regression test

- GL's readback is undefined where another window covers it, so the scenario refuses to run with
  another engine open and `fuactl shot` warns. Overlap is one way to get a black GL capture; it is not
  the only one (next point).
- With Vulkan on, GL captures on the template's skybox map are unreliable in every build: stale, black,
  or scrambled from one run to the next, with no other window open, and the old binary does exactly the
  same. They were twice blamed on this change before the old build was run under identical conditions.
  The scenario takes its GL references from a launch with Vulkan off, which is reliable.

## Navy sky looking up on wdiexsec (2026-10-09): a mirror's walk replaced the player's sky walls

Looking up in wdiexsec, Vulkan showed the clear colour (0.05, 0.06, 0.09) over most of the sky with a
few wedges of real sky. Two causes, both fixed:

1. **The list was refilled by the wrong walk.** The map's OPEN script calls
   `Sector_SetPlaneReflection`, and GL renders a reflective floor as a plane-mirror portal that walks
   the scene again from the mirrored eye. That walk was not a skybox, so it cleared the sky list and
   left its own quads: 88 from the player, 4 from the mirror, and Vulkan drew the 4. Only the
   player's outer walk (portal depth 0, main view) now fills the player's list; a camera texture fills
   its own (`ComputeSkyCaptureList`), and `fua_sky_quads` reports both.
2. **Nothing painted what no sky wall covers.** GL paints its first sky portal full-screen before the
   scene (`RenderFirstSkyPortal`), so the cone round the zenith that the 32768-high walls never reach is
   sky. Vulkan skipped that background to avoid the waterline streak; that streak came from the
   `Line_Horizon` slab fixed above, and at rc4l's skyline vantage painting it now changes the frame by
   0.06. The background is painted again, with the surfaces still cutting what lies beyond the sky.

Regression maps: `tools/fuactl/scenarios/skymirror.mjs` (reflection off against on) and
`skyzenith.mjs` (surfaces off against on, looking straight up).
