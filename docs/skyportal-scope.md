# The sky on wdiex02 is a PORTAL, not a texture

GL renders it. The backend draws a blue-and-white checkerboard, because it is falling back to a
placeholder texture that was never meant to be seen.

## What is actually happening, proved rather than assumed

The backend reports its sky per level, and on this map it says:

```
vulkan sky: -NOFLAT- 16x16 skyoffset 0  cap 172,201,208 / 172,201,208
```

`-NOFLAT-` at 16x16 is the null placeholder. The map author set no sky texture because the sky
does not come from one. Three captures from the same camera settle it:

| | sky |
|---|---|
| GL, `gl_noskyboxes 0` (default) | dark red cloudscape |
| GL, `gl_noskyboxes 1` | blue/white **checkerboard** |
| Vulkan | the same checkerboard |

Turning GL's skybox support off makes GL draw exactly what the backend draws. So the sky here is
a **ZDoom sky portal** -- an `ASkyViewpoint` actor whose view of the world IS the sky -- and the
backend has no portal support, so it renders the fallback.

It is not Transfer_Sky: `fuactl lines --special 271` and `272` find none of either on this map.

## GL picks a sky five ways. The backend implements the last one.

`gl/scene/gl_sky.cpp:80` onwards, in order:

1. **Sky portal** -- `sector->GetSkyBox(plane)` returns an `ASkyViewpoint`; the sky is the world
   as seen from that actor. **This map.**
2. **Transfer_Sky** -- `sector->sky` with `PL_SKYFLAT` encodes a linedef index, and the sky is
   that line's sidedef TOP texture (BOTTOM under `LEVEL_SWAPSKIES`), with that sidedef's offsets
   and a mirrored flag from `args[2]`.
3. **Double sky** -- `LEVEL_DOUBLESKY`, two layers.
4. **Swapped** -- `sky2texture` in place of `sky1texture`.
5. **Plain** -- `sky1texture`. This is the only branch the backend has:
   `FTextureID id = sky1texture;`

Branches 1-4 are all silently wrong today. Only 1 is needed for this map, but 2 is cheap and
worth taking in the same pass.

## What a sky portal is

`GLSkyboxPortal::DrawContents` (`gl/scene/gl_portal.cpp:647`) renders the whole scene again from
the viewpoint actor:

- position and angle are INTERPOLATED from the actor's previous tic, and the actor's angle is
  ADDED to the camera's -- which is how a rotating or parallaxed skybox works
- `viewz` is clamped to at least 4 units inside the skybox room's own floor and ceiling
- `extralight = 0`, depth clamp off
- recursion is bounded twice: `MF_JUSTHIT` on the actor, and a hard `skyboxrecursion >= 3`

## The design: this is the mirror pass again

`RenderMirrors` already does the whole shape -- save the MVP, build a different camera, render
the world into an offscreen colour+depth target, restore. A sky portal is that with a different
camera and no clip plane, so the new code is a camera and a target, not a subsystem.

**And the backend can be simpler than GL here.** GL needs stencils because it draws sky surfaces
in place, among the world. This backend draws the sky FIRST, full-screen, with depth off, and
then paints the world over it. A skybox is at infinity, so its rendered image IS that background:

1. render the world from the `ASkyViewpoint` into an offscreen target
2. blit that target where the dome would have gone

No stencil, no per-surface portal, no change to the frame structure. The existing sky pipeline
already draws a full-screen-ish thing at the right moment; this swaps what it samples.

## The work

1. **Find the skybox** -- `sector->GetSkyBox(plane)` for the camera's sector, honouring the
   `DefaultSkybox` fallback and the `SECF_NO*SKYBOX` flags that `GetSkyBox` already handles.
2. **An offscreen target**, sized to the swapchain, exactly as `g_mirrorColor`/`g_mirrorDepth`.
3. **The viewpoint camera** -- interpolated position and added angle. Use the existing tested
   `zx::InterpolateAngleBAM` rather than a second implementation; a hand-rolled BAM lerp is
   already documented in this tree as having made a rotating portal overshoot on right turns.
4. **Render the world into it** with the same call the mirror pass uses.
5. **Draw it as the sky** instead of the dome, and keep `DrawSkyFade` -- the fog sheet is
   independent of which sky is underneath.
6. **Bound the recursion** the way GL does: skip if the viewpoint's own sector is sky, and cap
   the depth.

## Cost, and the lesson that applies directly

This adds **one full scene render per frame**. That is precisely the shape that produced the GPU
timeout documented in `tdr-crash-after-map-loads.md` -- six mirrors re-rendering a 50,000-piece
scene, which crossed the two-second limit at a level transition and had the driver terminate the
process.

So it must be bounded from the start, not afterwards:

- **Skip when no sky is visible.** If nothing in view is sky-flagged, the portal costs nothing.
  The mirror pass had no visibility test at all and that was the entire bug.
- **Consider half resolution.** A skybox is at infinity and barely magnified; the mirror target
  is already a separate texture, so the precedent for a differently-sized target exists.
- **One skybox per frame**, chosen from the camera's sector. Sectors may each name their own
  (`CeilingSkyBox`/`FloorSkyBox`); rendering one per sector would multiply the cost by the thing
  that already broke. This is the same simplification the single-dome sky makes today.

## How it is judged

- The `gl_noskyboxes` test above is the harness: GL with skyboxes ON is the target picture, and
  the backend should match it rather than the checkerboard.
- A matched pair at `-4000,1800` facing 0 on wdiex02, sky band cropped, against the map's reload
  floor.
- **Two capture-tool limits to fix or work around first**, both hit while diagnosing this:
  `fuactl shot` renders its GL capture at pitch 0 whatever `--face` says, so a pair looking up
  cannot be matched; and in `--side-by-side` mode the Vulkan capture comes back 1x1, so a
  side-by-side session cannot be used for pixel work at all. Both need an embedded instance with
  `fua_dg_standalone 0`, or the GL half is a dead buffer.

## Test content

`wdiex02` is the case. `wdi03` covers the OTHER kind of skybox -- an `FSkyBox` of six textures,
which is a different feature entirely and is already implemented; do not confuse the two while
testing.

## What building it actually taught (2026-08-23)

The shader work and the viewpoint maths were the easy half. Two structural rules cost the whole
debugging session between them, and both are properties of the frame, not of the sky.

**1. A second view cannot be rendered from inside the first.**

The obvious place to hang the portal is the sky hook -- it is where the sky is decided. But that
hook lives inside `DrawWorld`, and rendering the portal means calling `DrawWorld` again. The
re-entry is not merely wasteful: `DrawWorld` derives the frame once per call (moved geometry, the
dynamic light list, the light clusters, the constant buffer), so the inner call leaves the outer one
holding the SKYBOX ROOM's derivation and drawing it with the player's camera. The map vanishes and
nothing in the log says why. The portal belongs BESIDE the frame, called from `DrawSceneOnce`
before its render targets are bound -- exactly where the camera-texture pass already sits, and for
exactly the same reason. `RenderSkyPortal` renders; `BlitSkyPortal` lays the result down in the sky
hook. Two functions, no re-entry.

**2. Never ask for a resource transition inside the frame's render pass.**

With the re-entry gone the portal rendered correctly and STILL erased the world behind it. The
cause was one argument: the blit committed its SRB with
`RESOURCE_STATE_TRANSITION_MODE_TRANSITION`, and the portal texture was still in RENDER_TARGET
state from the pass that had just filled it. Diligent honours that by ending the frame's render
pass to place the barrier and beginning a fresh one -- so every world draw that followed the sky
landed in a pass the frame no longer owned. Vulkan validation says nothing; the picture is simply
the sky, full screen. The fix is to hand the picture over at the end of `RenderSkyPortal`, outside
any pass, with an explicit `TransitionResourceStates`, and let the blit commit with
`RESOURCE_STATE_TRANSITION_MODE_VERIFY`.

The tell for both bugs is the same and worth remembering: **the sky is correct and the world is
gone**. That is never a sky bug. It means the pass the world was supposed to draw into is not the
pass that got presented.

## Measured

On `wdiex02` at `1162,163` facing `0,20` -- the water's edge, sky band visible -- with the sim
frozen and `fua_dg_standalone 0` so GL's half of the pair is a live buffer:

- portal on vs portal off, Vulkan against Vulkan: **0.9% of pixels differ**, and the worst row is
  `y=13`, in the sky band. The portal is on screen, and it touches nothing but the sky.
- against GL at the same camera: `mean|d| 11.5`, best alignment `dx=0 dy=0`, with the row profile
  putting the difference at the horizon (`y=156..170`) rather than in the sky. That residual is the
  pre-existing GL/Vulkan disagreement about wall tops, not the portal.
- `wdi03` still draws its world and still takes the dome path (`vulkan sky: SKY1 256x128`), and the
  dome path on `wdiex02` is reachable at any time with `fua_dg_skyportal 0`.
