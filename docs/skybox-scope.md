# Skyboxes under Vulkan: what it takes

GL renders them correctly. The backend detects one and then draws it as an ordinary cylindrical
sky dome, so a six-sided skybox comes out as one of its faces smeared around a cylinder. The
engine says so itself, once per sky, and has since the sky path was written:

```
vulkan sky: OVERCAST 512x512 skyoffset 0  cap 32,32,32 / 33,33,33  [SKYBOX -- not implemented]
```

That line is on `wdi03` today. It is a marker left deliberately, not a discovery -- what follows
is the work behind it.

## What a skybox IS in this engine

`FSkyBox : FTexture` (`gl/textures/gl_skyboxtexture.h`) -- six `FTexture *faces[6]` and a
`fliptop` flag. It carries no pixels of its own, which is why a sky named for one prints odd
dimensions.

GLDEFS builds them; the sky texture a map names simply resolves to one. There are **two forms**,
and `RenderBox` (`gl/scene/gl_skydome.cpp:343`) tells them apart by whether `faces[5]` exists:

| form | sides | top | bottom |
|---|---|---|---|
| six-face (`faces[5]` set) | `faces[0..3]`, one per wall | `faces[4]` | `faces[5]` |
| wrap (`faces[5]` null) | `faces[0]` across all four, U stepping 0, .25, .5, .75, 1 | `faces[1]` | `faces[2]` |

Every face is its own material, sampled CLAMP_XY -- clamped both ways, or the seams bleed. The
cube is 128 units each way, centred on the camera, and GL puts a model matrix on it rotating
`-180 + x_offset` about `glset.skyrotatevector`, which defaults to the Y axis. `fliptop` flips V
on the top face.

## What the backend has today

| | |
|---|---|
| geometry | one cylindrical dome, `BuildSkyDome(width, height)` |
| vertex | `SkyVertexData { x,y,z, u,v, r,g,b, useTex, alpha }` |
| pipeline | `g_skyPSO` -- depth test **off**, depth write **off**, cull **none**, blended |
| sampler | `AddressU = WRAP`, `AddressV = CLAMP` |
| binding | ONE `g_skySRB` with a single `uTex` |
| extras | `DrawSkyFade` (the fog sheet GL draws over the sky), cap colours for the dome ends |

## What is already right, and worth not rewriting

Most of the hard parts are done, which is why this is a small job rather than a new subsystem:

- **The vertex shader already centres on the camera and rotates about Y.** `kSkyVS` does
  `gl_Position = uMVP * vec4(p + uCameraPos.xyz, 1.0)` after rotating (x, z) by
  `uLightParams.y`, which is the sky x_offset. `glset.skyrotatevector` defaults to the Y axis, so
  for every skybox that does not override it **the rotation needs no new code at all**.
- **The pixel shader already handles a plain textured surface.** `vSkyColor.a == 1` selects the
  texture; a box face is exactly that. No shader change.
- **The pipeline state is already correct for a box** -- depth off, cull none, which is precisely
  what a cube centred on the camera wants.
- **Per-material bindings already exist.** `GetMaterialSRB(pso, material)` is what the world
  batches and the 2D layer use, and six faces are six calls to it.

## The work

1. **A box mesh builder** beside `BuildSkyDome`, emitting the same `SkyVertexData` with
   `useTex = 1`. The pipeline is `TRIANGLE_LIST`, so the `RenderBox` strips become lists: 6 verts
   per face, 36 for the six-face form; for the wrap form 24 for the sides plus 6 and 6.
2. **A clamped pipeline.** `g_skyPSO` wraps U, and a box face must clamp both ways. One more PSO
   with `AddressU = CLAMP`, otherwise identical. Precedent: the 2D PSO already clamps both.
3. **Six bindings and six draws** -- `FMaterial::ValidateTexture(sb->faces[i], false)` then
   `GetMaterialSRB`, exactly as a batch does. Three draws for the wrap form.
4. **Branch on `bSkybox`** where the marker prints today (`dgscene.cpp:1918`), choosing box or
   dome. The dome cap colours stop applying -- a box has real top and bottom faces.
5. **`fliptop`** -- flip V on the top face.
6. **Keep `DrawSkyFade`.** It is the fog sheet over the sky and is orthogonal to which sky is
   drawn.

## Recommendation

Do all six as one change. There is no half of this worth landing alone, and it is bounded: one
mesh builder, one pipeline, one branch, six draws. No new shader, no new vertex format, no change
to the frame structure.

**Defer** a non-default `skyrotatevector`. The shader Y rotation covers the default; an arbitrary
axis needs a real matrix in the sky constants, and no test content here uses one. Note it rather
than build it.

**Out of scope:** the second sky (`sky2`) used by double-sky maps, which the backend does not
draw at all today.

## How it is judged

The same way everything else in this port has been -- a matched GL/Vulkan pair from one camera
with the sim frozen, diffed against the map own reload floor:

- The `[SKYBOX -- not implemented]` line stops printing.
- `fuactl shot` on `wdi03` from an **outdoor** vantage: the pair should fall to the reload floor.
  Getting the camera somewhere the sky fills the frame is the fiddly part -- two attempts at this
  landed inside a hedge maze with no sky in shot, and `player.setpos` with only a `pitch` did not
  turn the view.
- Sanity: the six face names appear where the single sky texture name prints now.

## Test content

`wdi03` is the case (OVERCAST, flagged a skybox today). `wdi_bigpackv3.9test.pk3` carries
`TEXTURES/SKYBOXES/` cube faces and the phobos pk3 carries `PSKY1..6`. Both are already in the
Whodunit load order used throughout this work.

## A separate bug, not this one

`wdiex02` prints `vulkan sky: -NOFLAT- 16x16` with **no** skybox marker -- its sky is a 16x16
texture stretched over the dome, which is the vertical banding visible in the overview
screenshot. That is a different defect and fixing skyboxes will not touch it.
