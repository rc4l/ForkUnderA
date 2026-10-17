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

- `r_utility.cpp`: `R_SetupFrameFromRecord` sets a view up from a record alone (GL's `R_SetupFrame` takes an actor
  and also moves the live game and bumps `validcount`); the copied light lists and height transfers set the blend;
  `R_ClearPastViewer` works by id; `FRenderer::RenderCameraRecord` draws a camera texture from a record.
- `d_main.cpp` `D_PrepareDisplay` and `features/gameloop/loop.cpp` `AfterTic`: `FillMap` between tics.
- `features/frame/playerview.cpp`: `BeginRecordViewpoint`, and the camera textures from `CameraTextures`.
- `gl/scene/gl_scene.cpp` `FGLInterface::BeginFrame`: GL finds its live particles' subsectors itself.
- `p_effect.h`: guarded, and `ActiveParticles` declared for the particle copy.
- `features/gamecopy/gamecopy.cpp` `InitLevel`: room for the level's new textures and translations
  (`FTextureManager::ReserveHeadroom`), so the arrays a frame reads do not move during a tic.

- `d_main.cpp` `D_Display`: `FillMap` and `CheckBeforeViews`, after the server's early return.
- `r_utility.cpp` `R_SetupFrame`: `BlendMap` and `CheckAfterInterpolation` after `DoInterpolations`.
- `r_utility.cpp` `R_InterpolateView`: a frozen level and a game mode's end-of-level pause from the copied level
  (`flags2`, `endLevelDelay`).
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
  counts them, which `fuactl fixture seencheck` holds above zero on Vulkan alone. While it is on, a view GL does not
  draw moves the live game too (`CopyCheckOn`), so the blends are still held to the interpolations.
- Every actor gets an id at its first fill (`AActor::rcSerial`, `computation/serial_compute`), never reused, which
  the renderer's caches key on (the view interpolation, the camera change, the damage tint, light links);
  `RecordById` finds one, counting a miss in `copy/stale_ids`. `CopyTic` and `CopyLevel` say what the copy is of.
- `xfloors.cpp`: each sector's 3D floors and light list copied into `extsector_t::rcXFloor`, their planes, textures
  and light levels pointed at the copy's (`computation/memberindex_compute`), the colormaps their `GetColormap` gave
  kept beside them, and blended with their models' planes; `GetPlaneLight` and `HeightSecOf` read them.
- `lights.cpp`: every dynamic light and each subsector's light list, its links kept while it stands still.
- `particles.cpp`, `decals.cpp`: the particles with their subsector lists, and each side's wall decals.
- `tintsubject.cpp`: features/damage-tint's game half, read into each actor record: the sector damage table and
  whether an actor's floor hurts it unprotected (`TintSubjectOf`).
- `level.h`: the level's clocks, flags, sky scroll, fog and skies (`ZX_LEVEL_FIELD` lines in `fields.h`).
- `seen.cpp`: the seen bits the walks mark, kept by the renderer and merged into the live map before each tic
  (`ApplySeen`) and at each fill (`MergeSeen`), which also takes back what a load or reset changed.
- `polyobjs.cpp`: every polyobject linked and the BSP of each subsector one is in built between tics, then copied,
  its vertices riding their polyobject lines (`computation/polyblend_compute`) as the blend moves them.
- Sky viewpoints (`SkyBoxOf`, `DefaultSkyBox`) and camera textures (`CameraTextures`) are records too.

<!-- boundary:begin (python3 tools/boundary_tripwire.py --doc writes this section) -->
## What crosses the boundary

Generated from the field tables and the `ZX_SEAM` declarations; do not edit by hand.

Copied by `features/gamecopy/fields.h`:

- actor: `x`, `y`, `z`, `PrevX`, `PrevY`, `PrevZ`, `velx`, `vely`, `velz`, `angle`, `PrevAngle`, `pitch`, `roll`, `PrevRoll`, `sprite`, `frame`, `scaleX`, `scaleY`, `RenderStyle`, `renderflags`, `alpha`, `fillcolor`, `Translation`, `flags`, `flags2`, `flags3`, `flags5`, `state`, `tics`, `radius`, `height`, `floorclip`, `floorz`, `ceilingz`, `Speed`, `Sector`, `subsector`, `reactiontime`, `health`
- actor, worked out by the fill: `who`, `id`, `type`, `target`, `nextInSector`, `shownSprite`, `shownScaleX`, `shownScaleY`, `visible`, `isPlayer`, `playerIndex`, `inventory`, `cameraIcon`, `bobTic`, `attackRadius`, `attackHeight`, `playerBody`, `tintActive`, `tintTexture`
- decal: `LeftDistance`, `Z`, `ScaleX`, `ScaleY`, `Alpha`, `AlphaColor`, `Translation`, `PicNum`, `RenderFlags`, `RenderStyle`, `Sector`
- level: `time`, `maptime`, `flags`, `flags2`, `skyspeed1`, `skyspeed2`, `fadeto`, `outsidefog`
- level, worked out by the fill: `sky1`, `sky2`, `endLevelDelay`
- line: `flags`, `special`, `Alpha`, `args`
- player: `viewz`, `playerstate`, `cheats`, `bSpectating`, `extralight`, `fixedcolormap`, `fixedlightlevel`, `DesiredFOV`, `FOV`, `centering`, `MinPitch`, `MaxPitch`, `timefreezer`, `BlendR`, `BlendG`, `BlendB`, `BlendA`
- player, worked out by the fill: `who`, `number`, `mo`, `camera`, `lightPower`, `hasWeapon`, `weaponFOVScale`, `weaponType`, `weaponYAdjust`, `hudModel`, `bobIn`, `layers`, `screenBlend`
- sector: `planes`, `floorplane`, `ceilingplane`, `ColorMap`, `special`, `lightlevel`, `MoreFlags`, `Flags`, `reflect`
- side: `textures`, `Light`, `Flags`
- vertex: `x`, `y`

Seams:

- `copy/blend` (Main), `features/gamecopy/gamecopy.cpp`
- `copy/fill` (Handoff), `features/gamecopy/gamecopy.cpp`
- `copy/seen` (Handoff), `features/gamecopy/seen.cpp`
- `font/load` (Main), `v_font.cpp`
- `gc/collect` (GameData), `dobjgc.cpp`
- `gc/full` (Handoff), `dobjgc.cpp`
- `gc/object` (GameData), `dobject.cpp`
- `handoff/after_tic` (Handoff), `features/gameloop/loop.cpp`
- `handoff/before_tic` (Handoff), `features/gameloop/loop.cpp`
- `handoff/effects` (Handoff), `features/handoff/effects.cpp`
- `handoff/hud_record` (Handoff), `features/gameloop/hud.cpp`
- `handoff/snapshot` (Handoff), `features/handoff/snapshot.cpp`
- `handoff/ticker_begin` (Handoff), `features/gameloop/loop.cpp`
- `handoff/ticker_end` (Handoff), `features/gameloop/loop.cpp`
- `sky/map` (Main), `r_sky.cpp`
- `sound/channels` (GameData), `s_sound.cpp`
- `sound/deferred` (Handoff), `s_sound.cpp`
- `sound/music` (GameData), `s_sound.cpp`
- `texture/grow` (Idle), `textures/texturemanager.cpp`
- `tic/playsim` (GameThread), `features/gameloop/loop.cpp`
- `translation/player` (Main), `r_data/r_translate.cpp`
- `wad/read` (Main), `resourcefiles/resourcefile.cpp`
- `wad/read` (Main), `w_wad.cpp`
- `wipe/randoms` (Idle), `features/wipe/wiperandoms.cpp`
<!-- boundary:end -->
