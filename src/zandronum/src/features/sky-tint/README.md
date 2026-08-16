# features/sky-tint

Derives an outdoor light colour from the sky texture and applies it to sectors that can see the sky,
bleeding inward through real openings. Off by default; `cl_fua_skytint` turns it on.

## Why it is only a single colour

What a renderer really wants from a sky is *irradiance*, which depends on the surface normal, and
the industry answer is spherical harmonics (L2, nine coefficients) or an ambient cube. One averaged
colour is the L0 term of that expansion.

We stop at L0 because Doom's lighting model cannot hold anything more: a sector carries **one** light
colour, applied to floor, ceiling, walls and sprites alike, and there are no normals in it. Given
that ceiling, the useful work is choosing *which* pixels to average and how, which is what
`computation/skytint_compute` does.

| Knob | cvar | What it changes |
|---|---|---|
| On/off | `cl_fua_skytint` | |
| Strength | `cl_fua_skytint_strength` | how far from white the open-sky sectors go |
| Max colour | `cl_fua_skytint_saturation` | ceiling on saturation, so a violent sky cannot filter the level |
| Follow room light | `cl_fua_skytint_lit` | how much a sector's own light level scales its tint |
| Indoor reach | `cl_fua_skytint_reach` | how far inward the light travels, in map units |

Averaging happens in **linear light**, not sRGB bytes. Summing gamma-encoded values averages the
encoding rather than the light and lands too dark; `gl_texture.cpp`'s `averageColor` does exactly
that, which is why this does not reuse it.

## Four knobs that were removed

Each was measured before being cut, and each is gone at the value its default already had, so a
player who never touched them sees no change.

**Sky area** (horizon band vs cosine-of-elevation) measured 0.19 and 0.21 apart out of 255 on two of
the three skies tested. That is noise. It was also half of the pair that produced the failure below.

**Follow sky brightness** was built to let a dark sky tint more gently than a bright one, so that
Speed of Doom MAP01 and MAP20 would stop looking alike. Measured, it cut the tint by 90% and 86% on
those two maps: it scaled everything down without discriminating, which the plain Strength dial
already does more legibly. `StrengthForSectorLight` is what actually separates them.

**Sky colour from** offered a dominant-colour mode that scored buckets by pixel count. A quarter of
GSKY1 -- the sky on several GvH maps -- is near-black, so it returned `(2,2,2)`, and
`NormaliseBrightness` turns any near-black *neutral* into pure white by plain division. White is the
no-tint value, so the feature silently switched itself off on those maps and no slider could bring it
back. A mean cannot fail that way: a dark region drags the average down, it cannot capture it.

**Doorway matters** scaled how much a narrow opening slowed the light. It was a sub-dial of Indoor
reach and was never verified independently of it.

## Skyboxes

A sector can render a 3D skybox instead of a texture: the engine draws the level from a
`ASkyViewpoint` actor and shows that where the sky would be. There is no image to average, so the
only honest answer is to render it and look, which `SkyTint_FrameHook` does -- a 32x32 canvas texture
registered against the viewpoint, rendered by the engine's own camera-texture pass, read back off the
GPU and averaged like any other sky.

It goes through `FCanvasTextureInfo::Add` rather than calling `RenderTextureView` directly, because
`UpdateAll` saves and restores the `fixedcolormap` globals that camera rendering clobbers. It is also
self-limiting: nothing draws the texture on a surface, so nothing re-arms `bNeedsUpdate` and it
renders once rather than every frame. No row weighting is applied -- row weights describe position on
the sky *dome*, and a camera frame of a room has no such geometry.

This is not a rare case. 9 of the first 20 GvH Legacy of Darkness maps use skyboxes; none of epic2,
BTSX E1, Ancient Aliens, JPCP, Doom VII, HR2 or AV do, across 35 maps sampled.

## Which sky, per sector

There is no such thing as "the level's sky". A map can show different skies in different sectors
(`Init_TransferSky`, `p_spec.cpp`), draw `sky2texture` instead of `sky1texture` (`LEVEL_SWAPSKIES`),
or layer the two (`LEVEL_DOUBLESKY`). Reading the global `sky1texture` got the first case wrong, the
second case *completely* wrong (a different image from the one on screen), and ignored the second
layer of the third.

`SkyForSector` mirrors `GLWall::SkyPlane` in `gl/scene/gl_sky.cpp` to answer this. It mirrors rather
than calls, deliberately: extracting a shared helper would mean refactoring a vendored renderer file
we re-sync, and a permanent conflict there costs more than this feature is worth. The trade is that
it can drift, so it carries a `PROVENANCE`/`ON PORT` note saying to re-read `gl_sky.cpp` if upstream
changes sky selection.

Each distinct sky becomes a Dijkstra seed carrying its own colour and strength, so a leaf takes the
colour of whichever sky reaches it first. **Nearest source wins**; where two fronts meet there is a
seam, not a gradient. Blending by relative distance would be a deliberate addition and is not here.
Levels with one sky, which is nearly all of them, resolve to one source and cost what they always did.

## Why nothing is written to the world

The tint never touches `sector_t::ColorMap`. Sector colours are serialised into savegames
(`p_saveg.cpp`), so baking would mean a save taken while the option was on carries the tint forever,
and on reload those sectors are no longer "default white" -- so the check that politely defers to a
mapper's own colour would start deferring to its own past output. There is no way back from that.

Instead the colour is substituted into the render copy at the places the GL renderer reads a
sector's colormap. It is stored per SUBSECTOR -- the BSP leaf the renderer actually draws -- because
a whole sector taking one value reads as a flood rather than as light: a room beside a lit yard came
up evenly bright to its far corner. `GLFlat::DrawSubsector` re-issues the colour per leaf, which
costs one state change per lit leaf and nothing when the feature is off. Turning the option off restores the map exactly, and a savegame never learns the
feature exists. It is also client-side by construction: nothing is simulated, nothing is sent, and
two players disagreeing about it costs nothing.

## In-place edits outside this directory

- `gl/scene/gl_flats.cpp`, `gl_walls.cpp`, `gl_sprite.cpp`, `gl_drawinfo.cpp` -- one gated
  `zx::SkyTint_Apply` line after each `Colormap = <sector>->ColorMap`, 8 in total.
- `p_setup.cpp` -- `SkyTint_Rebuild()` at the end of `P_SetupLevel`.
- `gl/scene/gl_scene.cpp` -- `SkyTint_FrameHook()` immediately after `FCanvasTextureInfo::UpdateAll()`,
  which is where a registered skybox camera has just been rendered and can be read back. Beside the
  existing `hitboxviz::BeginFrame()` hook; that file has no upstream counterpart, so it is not a
  re-sync risk.
- `cl_main.cpp`, `p_acs.cpp`, `c_cmds.cpp` -- `SkyTint_SkyChanged()` after each `R_InitSkyMap()`
  that follows a real sky swap: the server's `SetMapSky`, ACS `ChangeSky`, and the `changesky`
  console command. The table is derived from `sky1texture`; these three change it without the level
  changing, and the client one fires on every map a client loads.
- `features/wadreload/zx_wadreload.cpp` -- `SkyTint_Clear()` in `ResetStartupStateForRestart()`, so a
  per-subsector table cannot outlive the subsector array across an in-process restart.
- `wadsrc/static/menudef.txt` -- the `FUASkyTintOptions` submenu, under FUA Options. Not
  Display Options: that menu is Zandronum's and `menudef.z` redefines it, so FUA features keep to
  their own room and a re-sync never has to untangle ours from theirs.

## Sectors it leaves alone

A sector whose colour was set by the mapper or a mod (`Sector_SetColor`, ACS, a colormap in the map)
is skipped outright: theirs wins, and it is never used as a bleed source either.

That rule is enforced at **draw time**, not only when the table is built, because the two happen at
different moments. `P_SetupLevel` builds the table, but ACS OPEN scripts are started with
`runNow=false` (`p_spec.cpp:1835`) and do not run until the first tic. A `Sector_SetColor` in an OPEN
script -- a common way for mods to colour a level -- therefore lands *after* the table already
decided to tint that sector, and the tint used to multiply into their colour rather than defer to it.
`ApplyIndex` now checks the colormap it was handed, which the caller copied from the sector moments
earlier, so a non-white value means somebody set it whenever they set it. The build-time skip stays
as an optimisation; the draw-time check is what makes the rule true. The tint is also
multiplied into whatever a sector already had rather than replacing it, because it represents light
arriving from outside rather than a repaint.

## Custom palettes and colormaps

Neither needs special handling, which was worth establishing rather than assuming.

Custom **palettes**: `ReadSky` goes through `CopyTrueColorPixels`, whose base implementation resolves
indices against the live `screen->GetPalette()`, so a mod's PLAYPAL is already reflected. True-colour
skies carry their own RGB and never consult a palette.

Custom **colormaps** never collide with the tint, because the two Boom mechanisms land elsewhere:

- `Transfer_Heights` (linedef 242) writes `sector_t::bottommap/midmap/topmap`, a packed ARGB word
  used only for the viewer's fullscreen blend (`r_utility.cpp`, `gl_scene.cpp`). Nothing here reads it.
- `Static_Init` (linedef 190) sets the sector's `ColorMap` to a real colour, which the "a mapper
  coloured this, theirs wins" rule above already defers to.

`FColormap::blendfactor` looks like it should be the flag for this and is not: every producer in the
tree passes alpha 0, so it is always 0. `ApplyIndex` still checks it, as a tripwire for a future port
rather than a live guard, and says so at the call site.

## History

Grew out of `experiment/sky-autotint`, a `fua_skytint` CCMD that baked into `sector_t::ColorMap` for
quick console A/B. That prototype identified the draw-time overlay as the right shape; this is that
shape, plus linear-light averaging, the dominant-colour mode, the weighting choice and the
saturation clamp.
