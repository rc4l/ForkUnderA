# features/decals

Every decal in the level is made here, on walls, floors and ceilings alike, under one rule. The decal classes (`DBaseDecal`, `DImpactDecal`, `DFlatDecal`) keep their constructors from everyone but this module's `Maker`, and the old entry points (`DImpactDecal::StaticCreate`, `ShootDecal`, the projected decal spawners) are gone, so a new caller cannot place a mark around the rule.

- `decals.h`/`.cpp`: the entry points each source calls, which describe the hit and hand it to `Maker::Mark`:
  - `MarkShot` (a hitscan or a rail), `MarkMissile`, `MarkBlood`, `MarkSpray`, `ShootMark` (a map thing or a script);
  - `WallAcceptsClone`, which spreading asks before a clone goes on a neighbouring wall;
  - `SurfaceRefusesMarks`, the world pass's per-pixel bit (texFlags 16384, alpha 0.75 in the G-buffer normal), so a floor mark's projection does not paint a surface that refuses one.
- `Maker::Mark` takes every mark through the same steps: refused before its template is chosen where stock refused it then, chosen, judged on its surface, then made. A wall decal is stock's `StaticCreate`, its lower decal first and spread with `cl_spreaddecals`. A permanent decal sticks wherever its trace stopped, as stock's does. A floor or ceiling mark and its lower decals are `DFlatDecal`s.
- `flatdecal.h`/`.cpp`: `DFlatDecal`, a `DImpactDecal` in `STAT_AUTODECAL`. It is saved with the level (SAVEVER 4524; a build before it stops on the unknown class, and a build from it refuses any newer save), counted against `cl_maxdecals` with the wall decals and retired oldest first with them, run by every DECALDEF animator (a slider moves nothing, since a flat has no direction for it), and shaded the victim's colour as blood. It records where it landed, the plane's normal (tilted on a slope, turned to face the hit), the way the hit went, and the plane it rides with its height above it: a sector's floor or ceiling, or a 3D floor's model sector.
- `computation/markrules_compute`, tested: `ComputeMarkAllowed` and `ComputeSurfaceVerdict`, the one rule. A mark is refused, in this order:
  - on a server, for an impact;
  - with `cl_maxdecals 0`;
  - for a missile with `cl_missiledecals` off;
  - on a floor or ceiling with `fua_decal_flats` off;
  - on a `noautodecals` side, which a scroller also sets (wall);
  - on a horizon line, for an impact (wall);
  - in a two-sided line's open span (wall);
  - on sky;
  - on a terrain liquid;
  - on a plane that is a portal (floor or ceiling);
  - on a translucent, additive or fog 3D floor, its planes and its sides, where UZDoom and GL draw no decal;
  - on a plane a texture scroller moves (floor or ceiling);
  - on a texture refusing decals: ANIMDEFS animations, warps and animated doors without `allowdecals`, Boom ANIMATED walls without the bit, TEXTURES `NoDecals`.

  A permanent decal keeps stock's leave to ignore the budget, `noautodecals` and refusing textures. `ComputeRefusedBeforeChoice` is what stock refused before drawing a group's choice. `ComputeChoiceFromGameStream` keeps the game's random stream for the draws shipped builds made (walls, and shots' and missiles' floor marks) and gives the rest the renderer's (`ProjDecal`). `ComputeMissilePlaneHit`, `ComputePlaneOwner` and `ComputeMarksToEvict` decide which plane a missile rests on, which 3D floor a flat hit belongs to and how many marks to retire.
- `fua_decal_report` prints each surface's marks by verdict since the level began, and how many are alive; `fua_decal_report reset` starts the count again. Scenario `decalrules`, which judges counters only, so it runs on WebGPU too.
- Marks are the client's alone: a server judges its impacts `server` and holds none, while a client makes its own from its shots (`cl_hitscandecalhack`). Scenario `decalnet`, a hosted server and a joined client.
- Marks are made in the tic, on the game thread when it runs tics, and reach a frame only through the copy's fill. Scenario `decalrules` runs with tics on the game thread and inline, the same rules either way.
- A hitscan into a liquid splashes whether or not it marks the floor; scenario `decalrules` counts the splash, and fixture `gtmod` shoots a TERRAIN liquid from its script so the traces hold the splash.
- How UZDoom settles the cases it shares with this, and what was kept:
  - Decals are drawn in mirror, stacked and camera views, as UZDoom draws them: wall decals are the walls' own geometry, and the floor marks' pass reads each view's own G-buffer (`BeginPassTargets`, features/hwrender). Scenario `decalviews`.
  - A texture's refusal is checked when a mark is made, not again when drawn, so a wall decal stays when its texture comes to refuse decals, as UZDoom's does. A floor mark's picture follows the per-pixel bit, so a surface that comes to refuse marks stops showing one, its own included. Scenario `decalviews`.
  - Wall decals do not follow texture scrolling. A scroller sets `noautodecals` on its side (p_spec.cpp), and a scrolling plane refuses floor marks the same way. A mark stays where it landed when its surface's offsets or rotation change later, as a wall decal stays put on its wall.
  - Marks sit on real planes, never on Transfer_Heights' fake ones; the projection only shows them where the real plane is drawn. A mark on a slope faces along it. Scenario `decalrules`.
  - UZDoom has no floor or ceiling decals at all, so their rules are the wall rules carried over.

## In-engine hooks

- `p_map.cpp`:
  - `P_LineAttack`: `MarkShot` for a wall, floor or ceiling hit; stock's floor splash test runs beside it and is never skipped.
  - `P_RailAttack`: `MarkShot` wherever the endpoint puff goes (`ComputeRailEndpointSpawn`).
  - `P_TraceBleed`: `MarkBlood` for any surface its trace reaches.
- `p_mobj.cpp` `P_ExplodeMissile`: `MarkMissile`, with the velocity it had before it stopped.
- `d_net.cpp` `DEM_SPRAY`: `MarkSpray`.
- `p_acs.cpp` `DoSpawnDecal`, `cl_main.cpp` `SVC2_SHOOTDECAL`, `g_shared/a_decals.cpp` `ADecal::BeginPlay`: `ShootMark`.
- `g_shared/a_decals.cpp`:
  - `CloneSelf` asks `WallAcceptsClone`;
  - `StickToWall` and `TierTexture` share the tier test the rule judges;
  - `CheckMax` retires through `ComputeMarksToEvict`.
- `g_shared/a_sharedglobal.h`: the decal classes befriend `zx::decals::Maker`.
- `decallib.cpp`: `ApplyToDecal` and `GetDecalWith` take the random stream; `weightedlist.h` `PickEntry(FRandom &)`.
- `features/gamecopy/fields.h` `ZX_FLATDECAL_FIELD` and `features/gamecopy/decals.cpp` `TakeFlatDecals`: the marks copied in the fill for the frame.
- `features/levelmesh/projdecals.cpp`: their boxes, from the copy.
- `features/hwrender/diligent/dgupload.cpp`, `dgscene_glsl.h`, `dgdecals.cpp`: the per-pixel bit.
- `p_setup.cpp` `P_SetupLevel`: `ResetDecalReport`.
