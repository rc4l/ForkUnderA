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
| Indoor bleed | `cl_fua_skytint_bleed` | how many rooms inward the light reaches, halving each step |
| Sky colour from | `cl_fua_skytint_mode` | Average (faithful) or Dominant (the colour a person would name) |
| Sky area | `cl_fua_skytint_weight` | Horizon (what the player sees) or Overhead (what lights a floor) |

Averaging happens in **linear light**, not sRGB bytes. Summing gamma-encoded values averages the
encoding rather than the light and lands too dark; `gl_texture.cpp`'s `averageColor` does exactly
that, which is why this does not reuse it.

Horizon and Overhead genuinely disagree on a sky with a coloured horizon: one is the mood the player
sees, the other is the light a floor actually receives. That is a look choice, not a right answer,
which is why it is a knob rather than a constant.

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
is skipped outright: theirs wins, and it is never used as a bleed source either. The tint is also
multiplied into whatever a sector already had rather than replacing it, because it represents light
arriving from outside rather than a repaint.

## History

Grew out of `experiment/sky-autotint`, a `fua_skytint` CCMD that baked into `sector_t::ColorMap` for
quick console A/B. That prototype identified the draw-time overlay as the right shape; this is that
shape, plus linear-light averaging, the dominant-colour mode, the weighting choice and the
saturation clamp.
