# features/gamecopy

What the renderer reads of the game, copied in the handoff once a tic, so a frame drawn while the next tic runs
never reads what that tic is changing.

- `fields.h` is the one declaration: each `ZX_COPY_FIELD(kind, name, ...)` line is a field the renderer reads,
  and from that line come the copy's struct, its fill, the comparison that tells it changed, its blend between
  two tics, its hash and its dump, `fua_copy_check`'s comparison against the live game, and the boundary
  tripwire's pattern for reading it live. `ZX_ACTOR_FIELD` and `ZX_ACTOR_DERIVED` lines declare an actor
  record's members, copied or worked out by the fill.
- Map structures keep their copy beside the live fields (`sector->rc`, `side->rc`, ...), so the renderer walks the
  same pointers and reads `rc`; actors are copied as records (`actors.h`), in thinker order and linked per sector
  in thinglist order, so the renderer holds no live actor.
- `select.h`: code GL and the backend both run takes a `Sel`, `LiveSel` for GL (the live structures, through the
  setters whose dirty counters GL's caches key on) or `CopySel` for the backend. GL's height transfers are scratch
  sectors whose copy is never filled, so a helper GL calls must read live.

## In-engine hooks

- `d_main.cpp` `D_Display`: `FillMap` and `CheckBeforeViews`, after the server's early return.
- `r_utility.cpp` `R_SetupFrame`: `BlendMap` and `CheckAfterInterpolation` after `DoInterpolations`.
- `features/frame/levelrenderer.cpp` `PreprocessLevel`: `InitLevel`.
- `r_data/r_interpolate.cpp`: each interpolation's `CopyForRender` adds its blend.
- `r_defs.h`: the `rc` state on sectors, sides, lines and vertices and their change counters.
- `actor.h`: `AActor::rcRecord` (its record, written between tics only) and `BobOffsetAt`.
- GL builds a record with `TakeActor` where it shares a helper with the backend: `gl/models/gl_models.cpp`
  (`ModelPoseFor`) and `features/hitboxviz/hitboxviz_gl.cpp` (`AppendActor`).
- `players.h`: each player's record and each view source (a player's camera or a camera texture's viewpoint)
  with the quakes shaking it, where the chase camera stands from it, and the weapon style its inventory gives.
  `r_utility.cpp` `R_SetupFrame` sets a view up from these and names its camera (`SetViewCamera`); the
  view setup, FOV, screen blend and weapon read them too.
- Game code split so the fill can take what it works out: `p_pspr.cpp` `P_WeaponBobInputs` and `WeaponBobFrom`,
  `v_blend.cpp` `V_PlayerBlendLayers` (the screen blends one by one, added again by `AddScreenBlend`), and
  `g_shared/a_quake.cpp` `FQuakeState` with `StaticGetQuakes` and `StaticQuakeIntensities`, so a frame sums
  the quakes at its own point through the tic.
- `fua_copy_check` ignores the seen bits the walks set on the live map (`ML_MAPPED`, `SECF_DRAWN`); `fua_seen_count`
  counts them, which `fuactl fixture seencheck` holds above zero on Vulkan alone.

<!-- boundary:begin (python3 tools/boundary_tripwire.py --doc writes this section) -->
## What crosses the boundary

Generated from the field tables and the `ZX_SEAM` declarations; do not edit by hand.

Copied by `features/gamecopy/fields.h`:

- actor: `x`, `y`, `z`, `PrevX`, `PrevY`, `PrevZ`, `velx`, `vely`, `velz`, `angle`, `pitch`, `roll`, `PrevRoll`, `sprite`, `frame`, `scaleX`, `scaleY`, `RenderStyle`, `renderflags`, `alpha`, `fillcolor`, `Translation`, `flags`, `flags2`, `flags3`, `flags5`, `state`, `tics`, `radius`, `height`, `floorclip`, `floorz`, `ceilingz`, `Speed`, `Sector`, `subsector`, `reactiontime`, `health`
- actor, worked out by the fill: `who`, `type`, `target`, `nextInSector`, `shownSprite`, `shownScaleX`, `shownScaleY`, `visible`, `isPlayer`, `playerIndex`, `inventory`, `cameraIcon`, `bobTic`, `attackRadius`, `attackHeight`, `playerBody`, `tintActive`, `tintTexture`
- line: `flags`, `special`, `Alpha`, `args`
- player: `viewz`, `playerstate`, `cheats`, `bSpectating`, `extralight`, `fixedcolormap`, `fixedlightlevel`, `DesiredFOV`, `FOV`, `centering`, `MinPitch`, `MaxPitch`, `timefreezer`, `BlendR`, `BlendG`, `BlendB`, `BlendA`
- player, worked out by the fill: `who`, `number`, `mo`, `camera`, `lightPower`, `hasWeapon`, `weaponFOVScale`, `weaponType`, `weaponYAdjust`, `hudModel`, `bobIn`, `layers`, `screenBlend`
- sector: `planes`, `floorplane`, `ceilingplane`, `ColorMap`, `special`, `lightlevel`, `MoreFlags`, `Flags`, `reflect`
- side: `textures`, `Light`, `Flags`
- vertex: `x`, `y`

Seams:

- `copy/blend` (Main), `features/gamecopy/gamecopy.cpp`
- `copy/fill` (Handoff), `features/gamecopy/gamecopy.cpp`
<!-- boundary:end -->
