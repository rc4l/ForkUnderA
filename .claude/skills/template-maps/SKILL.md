---
name: template-maps
description: Build a small generated map that isolates one engine feature or bug, and turn it into a GL-vs-Vulkan (or Vulkan-vs-Vulkan) regression test with fuactl. Use whenever a fault reproduces only in a mod's maps, when a feature needs a test level of its own, or when a renderer fix needs a regression test that runs from the repository alone (Freedoom 2 textures, no mod wads).
---

# Template maps: one ingredient, one map, one regression test

A fault found in a mod's level (wdiex02 needs nine wads) cannot be a regression test, and the big map
hides what matters behind everything else in it. A **template map** carries only the ingredient — one
line special, one sky type, one sector trick — built in code, with Freedoom 2 textures, so it runs
anywhere the repo does. The engine builds nodes itself for a UDMF map that has none.

It pays twice. The horizon template reproduced the reported bug AND crashed every plain-sky map under
Vulkan on its first load, a bug wdiex02 could never show because its sky is a skybox.

Worked example: `tools/fuactl/scenarios/horizon.mjs` (about 60 lines).

## 1. Reproduce in the real content first, and name the ingredient

Get the fault on your own screen in the reporter's map (`reproducing-a-report`), then find what it is
made of: which line special, sector flag, sky type, texture setup. Ask the engine rather than guess —
`fuactl line <index>` / `line --at X,Y`, `look`, `rpc state.actors`, the engine's own logs
(`fua_sky_log` named the horizon lines) -- and the map's own files: `fuactl mapdata <MAP> --preset ID
--sector N --scripts REGEX` found wdiexsec's reflective floor in its OPEN script. The template
reproduces THAT, nothing else.

## 2. Build the map with `src/udmf.mjs`

```js
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";   // specials and editor numbers by name, checked against the engine

const m = new UdmfMap();
// A closed room, walked clockwise so it is on every wall's right; walls(edge) styles each edge.
m.room(0, 0, 1024, 1024,
  { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
  (edge) => edge === "north" ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } }
                             : { side: { texturemiddle: "BROWN1" } });
m.thing({ x: 512, y: 724, angle: 90, type: 1 });          // player 1 start
const bytes = wad([...mapLumps("MAP01", m), ...mapLumps("MAP02", other)]);
```

- `room()` for closed sectors, `block()` for a sector standing inside another (two-sided lines, e.g.
  a pool or a tagged area), `skyboxRoom(x, y)` for a ready skybox every sky ceiling shows,
  `stackedRooms({ dx, upperWidth })` for a room whose ceiling is a stacked sector onto a sky-lit room
  `dx` east (it returns both sectors, for a `block()` inside the upper one), `wall()`
  for single one-sided lines (sector on the right), `opening()` for a single two-sided line (front on
  the right, each side styled, e.g. a window through a wall), `sector()`, `thing()` (present on every skill and
  mode unless told otherwise), `vertex()` shares corners.
- **Behaviour set by ACS** (reflective floors, anything only a script can do): `src/acs.mjs` writes an
  ACS0 BEHAVIOR lump with one OPEN script -- `acs0([...callSpecial(159, [tag, 66, 0]), PCD.TERMINATE])`
  -- passed as `mapLumps("MAP01", m, { behavior })`. No ACS compiler is needed. `PCD.DELAYDIRECT, n` waits n tics first, so a change lands
  after the level is baked -- which is how a fault in noticing a runtime change is caught at all. A small map can bake
  later than the delay; when the order matters, write a closed script (`acs0(code, [], { open: false, number: 2 })`)
  and give each side `commands: ["puke 2"]`, which run once the level is up and baked (`watertint.mjs`).
- **Texture both sides of every step** unless the missing texture is the point. GL closes an untextured
  upper or lower with the neighbour's plane (`HandleMissingTextures`, now baked too), so a pool whose inner
  steps have no `texturebottom` shows as flat floor in both renderers: `warpwater.mjs` showed water only
  while the bake skipped that fill. `missingupper.mjs`/`missinglower.mjs` are the fill on purpose.
- Any UDMF key passes straight through: `special`, `arg0..`, `texturetop/middle/bottom`, `lightlevel`,
  flags. Numbers stay bare and positions get a decimal point; a QUOTED number makes the engine drop
  the thing, and a map with no player start crashed on load.
- Useful types: player start `1`; `SkyViewpoint` `9080` (with no TID it is the level's default skybox,
  so a sky ceiling shows it). Sky ceilings use `F_SKY1`. Freedoom 2 has every Doom 2 texture name.

## 3. Design it so the fault cannot hide

- **One map per code path.** If the feature behaves differently by sky type, lighting mode or sector
  kind, make a map for each: a plain-sky MAP01 and a skybox MAP02 hit different code.
- **Put a witness where the bug would show.** The horizon template stands a tall bright room BEHIND the
  horizon line: GL never shows it, so any leak through the window is loud.
- **Light it.** Lightlevel 192–255 keeps distant detail from fading to black, which is where a wrong
  plane, UV or depth would otherwise hide.
- **Frame it.** Pick fixed vantages (`{ x, y, z, angle, pitch }`) where the ingredient fills a known
  part of a 1280x720 frame, and include one oblique view.

## 4. Turn it into a scenario with `src/template.mjs`

```js
import { runScenarioIfMain } from "../src/template.mjs";
runScenarioIfMain(import.meta.url, () => ({ name: "horizon", wad: buildHorizonWad(), maps: ["MAP01", "MAP02"],
  vantages: VANTAGES, boxes: BOXES }));
```

`runScenarioIfMain` runs only when the file itself is invoked (`node scenarios/horizon.mjs [port]`), prints
the engine it runs (the staged `dist-windows` one unless `FUACTL_ENGINE` names another) and each box's verdict, and exits 0 (all under ceiling), 1 (failed), 2 (crashed) or 3 (refused) -- so a test
can import the builder without launching anything. The whole set runs as one command,
`node src/cli.mjs scenarios [name ...] [--engine build-win/Release/forkundera.exe]`, which prints only
the boxes that did not pass and exits 1 if any scenario failed. Players run standalone and the
default suite does not, so also run `scenarios --standalone` for anything standalone touches: each
scenario's own `standalone` side, else its `vk` side with `fua_dg_standalone 1`, else skipped.

`compareTemplate` refuses to run while another engine is open (GL readback under another window is
undefined), launches each map twice — GL alone, then Vulkan with `fua_dg_standalone 0` — captures
every vantage, and judges each box by `boxDiff`: mean |d| in 16-pixel block means, so the renderers'
sub-pixel framing averages out while a block-sized fault stays loud. Its GL frames come from the
Vulkan-off launch on purpose: with Vulkan on, GL captures on a skybox map were unreliable in every build.
Every frame it judges is kept, as `<engine dir>/sweep/<name>_<map>_<vantage>_<side>.png`, so a failing box
can be looked at rather than re-shot by hand. "Another engine" includes the reporter's own side-by-side session: `quit --park --port P`
before a GL-side run and `play --unpark --port P` afterwards (`fua-side-by-side`), never by process name.

When the fault is a Vulkan switch rather than a GL difference, pass `sides`: two named launches, each
with the cvars it sets and any `commands` to run after them (`fua_mapbake_now` re-bakes with a bake
cvar changed), compared Vulkan against Vulkan. `scenarios/pointlight.mjs` compares
`fua_dg_clusters 0` against `1`, which match exactly once fixed, so its ceiling is 0.05. With no GL side
nothing is read back from a window, and the harness runs even with other engines open.
A side with `frame: "gl"` keeps GL's half of a Vulkan launch's pair instead. GL there draws its own
vertices and never the backend's wall mesh; `mapreload.mjs` (`commands: ["map MAP01"]`, a level change)
checks it still draws after the level loads again, and `glplain.mjs` checks it never reads the mesh. A side's
`atVantage` commands run once it stands at each vantage, for a change the view's own walk must see first
(`fua_mesh_blank` empties the mesh GL's capture just filled). `fuactl scenarios <name> --side S --cvar k=v`
adds cvars to one side, which is how a test shows it fails under the behaviour it guards against; a
scenario without that side is skipped rather than ending the run. A side with `cvars: { fua_dg_standalone: 1 }` judges
Vulkan carrying the frame alone; `standalonemove.mjs` runs its script after that, so the move is not baked in.
A standalone side that spawns facing away and then turns (`skymove`, `stackturn`, `skyboxturn`) catches
what the frame kept from GL's last walk instead of walking itself.

A side with `noGl: true` launches with `-vulkan`, which makes no GL context at all. The harness reads
`dumprenderer` and fails the side unless it prints `renderer=vulkan`, since an old binary ignores the flag
and runs GL; it leaves standalone on, the only frame such a launch has (`nogl.mjs`).

A box can name a `vantage`, when the vantages look at different parts of the frame. A model needs no
assets: `kvxBox` and `voxelLumps` in `src/kvx.mjs` put a KVX voxel on every frame of a sprite.

A side's `commands` can also load another map of the same wad (`map MAP02`), which makes the oracle a
level that is right by construction: `waterseam.mjs` judges a pool split into two sectors against the
same pool in one piece, and `underwatersprite.mjs` a medkit under all-but-opaque water against the
empty pool.

Binaries before be8a9500 drew Vulkan without GL's 1.2 pixel stretch, and binaries before 016073df
drew Vulkan's view behind the status bar instead of in the view window (`viewwindow.mjs`), so when a
scenario fails on an old binary check the framing first. Thin boxes still read sub-pixel framing
differences, so prefer a Vulkan-vs-Vulkan oracle that is right by construction for the
region (`water3d.mjs`: bands off is the wall's own light, which is what the part above the water must
have), and prove it fails on a deliberately broken build.

For a picture that changes between two captures of one paused frame (an ANIMDEFS texture runs on the
clock), `compareSameFrame` takes GL and Vulkan from ONE engine frame (`scrollwall`, `particlecolor`); it
refuses beside other engines and ignores `sides`, so `--side/--cvar` does nothing there.

For something that must MOVE (a warp, a scroller), use `compareOverTime` instead: one launch, each
vantage captured twice `intervalMs` apart, and a box can carry a `floor` (it must change at least this
much) as well as a `ceiling`. Judge it per pixel (`block: 1`): a ripple shifts an even texture without
moving its 16-pixel block means.

For a fault that lasts ONE frame (a sprite drawn white the frame its material first appears), no paused
capture can catch it: have the engine count it, and use `compareCounter`, which stands at each vantage
with the sim running and holds a console command's number to a ceiling (`counter: { command, label,
pattern, ceiling | floor }`, `pattern`'s first group the number), launched with the first non-GL side's
cvars. `spriteslots.mjs` reads `fua_dg_dynstats`; `particles.mjs` holds a floor.

`BOXES[map]` is `{ name: { box: [x0, y0, x1, y1], ceiling } }` in frame fractions. Set ceilings from
measurement, not taste: run the FIXED build, take what it reads, add margin. A map with a known,
unrelated difference gets its own ceiling (MAP01's dome reads ~10 in both builds; the horizon box ~2).

## 5. Prove it catches the bug

Keep a copy of the old `dist-windows/forkundera.exe` before staging the fix (see `zandronum-rebuild`),
run the scenario against it, and confirm it FAILS — the horizon one crashed on MAP01 and read 24 and 17
where the fixed build reads 2. A test that has never failed is not yet a test (`divergence-triage` §7).

## 6. Unit-test the builder

The wad builder is pure, so it gets a `node --test` case: the lumps are there in order and the
ingredient is in the TEXTMAP (`test/udmf.test.mjs` checks the horizon special and the skybox thing).
Export the builder from the scenario; `runScenarioIfMain` keeps the import from launching anything.

## Files

| file | role |
|---|---|
| `tools/fuactl/src/udmf.mjs` | UDMF map + PWAD writer |
| `tools/fuactl/src/template.mjs` | `compareTemplate` (launch, capture, judge), `compareOverTime`, `compareCounter`, `compareSameFrame`, and the pure `judge` |
| `tools/fuactl/src/pngcore.mjs` | PNG decode/encode, `boxDiff`; `fuactl png --boxdiff` on the CLI |
| `tools/fuactl/scenarios/horizon.mjs` | the worked example |
| `tools/fuactl/scenarios/pointlight.mjs` | two Vulkan sides, a light-grid switch against itself |
| `tools/fuactl/scenarios/skymirror.mjs` | a reflective floor set by an OPEN script, reflection off against on |
| `tools/fuactl/scenarios/skyzenith.mjs` | straight up where no sky wall reaches, sky surfaces off against on |
| `tools/fuactl/scenarios/water3d.mjs` | a swimmable 3D floor's light band on the pool walls, bands on against off |
| `tools/fuactl/scenarios/droppedbindings.mjs` | every binding dropped (`fua_dg_flushtextures`, what a minimise does) against kept, over still water |
| `tools/fuactl/scenarios/waterseam.mjs` | two 3D-floor waters meeting at a sector line against one pool: the seam draws no wall |
| `tools/fuactl/scenarios/underwatersprite.mjs` | a sprite under translucent water against no sprite: the plane-layer sort draws it first |
| `tools/fuactl/scenarios/underwaterup.mjs` | a water's underside from below, its water lit dark blue against lit like the pool: a top plane takes the light above it |
| `tools/fuactl/scenarios/floodgap.mjs` | steps with no lower or upper texture against the same hall with no steps; boxes name their `vantage` |
| `tools/fuactl/scenarios/planemirror.mjs` | a reflective floor before a raised block, GL against Vulkan |
| `tools/fuactl/scenarios/mirrorwalk.mjs` | a voxel model (`src/kvx.mjs`) in a room whose sky and floor are GL portals, against one with neither |
| `tools/fuactl/scenarios/switchtex.mjs` | a switch pressed with `+use` against the same wall drawn pressed |
| `tools/fuactl/scenarios/actorlight.mjs` | a sprite by a red point light, GL against Vulkan: the actor's own light |
| `tools/fuactl/scenarios/warpwater.mjs` | a warp2 flat captured twice apart (`compareOverTime`): the water must move, the floor must not |
| `tools/fuactl/scenarios/scrollfloor.mjs` | a floor scrolled by its sector type (`SECTOR_TYPE` in `src/doomnums.mjs`) captured twice apart: it must move, the floor beside it must not |
| `tools/fuactl/scenarios/walllight.mjs` | a wall lit by `Transfer_WallLight` from a sector a script brightens after load, Vulkan against the same wall lit 255 from the start: GL's own wall cache had the fault too, so GL is no oracle here |
| `tools/fuactl/scenarios/raised3dfloor.mjs` | a 3D floor platform a script raises after load (`FloorAndCeiling_RaiseByValue` on its control sector), GL against Vulkan |
| `tools/fuactl/scenarios/desaturate.mjs` | a sector with UDMF `desaturation` 0.99 (MAP01) and an out-of-range 99 (MAP02), GL against Vulkan |
| `tools/fuactl/scenarios/mirrorbehind.mjs` | a thing on its own block behind the player, which only GL's walk through the mirror reaches, GL against Vulkan |
| `tools/fuactl/scenarios/horizondeep.mjs` | a horizon line under a sky ceiling seen from well above its floor, square-on and at an angle (only the oblique view shows the sky quad and floor half depth-fighting), GL against Vulkan |
| `tools/fuactl/scenarios/skyboxopen.mjs` | a skybox room that is all sky (sky floor and ceiling, horizon walls) under a six-face GLDEFS skybox, GL against Vulkan |
| `tools/fuactl/scenarios/skywallbelow.mjs` | a sky wall over a closed sky sector that starts below the eye, with a lit room behind it GL never walks, GL against Vulkan |
| `tools/fuactl/scenarios/mirrorsky.mjs` | a mirror beside a window onto a low sky-roofed yard shallower than the player stands from the glass, so the sky GL sees through the window lies between the reflected eye and the mirror, GL against Vulkan |
| `tools/fuactl/scenarios/missingupper.mjs` | a raised-ceiling island with untextured uppers, seen from inside, GL (wall cache off, the stock fill) against Vulkan: the map bake must draw the room's lower ceiling across it as GL's missing-texture fill does |
| `tools/fuactl/scenarios/skyhole.mjs` | a one-sided wall with no texture under a sky, GL against Vulkan: on MAP01 a lit room behind it that nothing joins, which GL's walk clips, so the backend must show sky there rather than the room; on MAP02 (skybox) and MAP03 (plain sky) the yard's own sector reaches past it through a corridor, and GL draws a pillar in it and the field's floor and far wall through the hole, which the backend must keep |
| `tools/fuactl/scenarios/spriteslots.mjs` | animated decorations ahead of and behind the player (`compareCounter`): no sprite frame may sample a material slot the bindless array was not yet filled with |
| `tools/fuactl/scenarios/skyboxbeyond.mjs` | a skybox area with low walls and a tower in a sealed room beside it, GL against Vulkan: the sky over the skybox walls shows only what GL's walk from the skybox camera reached |
| `tools/fuactl/scenarios/transferheights.mjs` | a lake whose surface is a Transfer_Heights control sector's floor, seen from the shore, from under it and from a step 2 under the eye: the surface at the control height, underwater its textures and blue light, and nearer than the near plane still drawn, as GL's depth clamp keeps it |
| `tools/fuactl/scenarios/cameratex.mjs` | an ANIMDEFS camera texture twice as wide as tall, fitted smaller than its pixels, on a one-sided wall, set by an OPEN script's SetCameraToTexture: the screen must show the camera's room upright, framed and sharp as GL shows it |
| `tools/fuactl/scenarios/missingfillcache.mjs` | the missing-upper map in GL alone, wall cache off against on: the cache must not cost GL its missing-texture fill |
| `tools/fuactl/scenarios/missinglower.mjs` | a recess with untextured steps, GL against Vulkan: GL's missing-texture fill must run every frame under the wall cache. GL does not clear its framebuffer, so a missing fill shows stale pixels, and the map looks at a red wall first to put red there |
| `tools/fuactl/scenarios/mapreload.mjs` | a room loaded a second time inside a Vulkan session (`commands: ["map MAP01"]`), GL alone against GL's frame there: GL must still draw the world after a level change rebuilds the wall mesh |
| `tools/fuactl/scenarios/glplain.mjs` | a room reloaded in a Vulkan session with the map bake off, the mesh then emptied at the vantage (`atVantage: ["fua_mesh_blank"]`), GL alone against GL's frame there: GL must draw its own vertices, not the backend's mesh (queue #42) |
| `tools/fuactl/scenarios/standalonemove.mjs` | a platform a closed script raises (`puke 2`) after Vulkan has gone standalone, GL alone against the standalone frame: the move must show with no GL walk behind it |
| `tools/fuactl/scenarios/invertedback.mjs` | a wall onto a sector whose floor is above its ceiling, GL against Vulkan: both must draw it whole |
| `tools/fuactl/scenarios/watertint.mjs` | additive 3D-floor water whose control sector a puked script tints after the bake, over a grey pool floor, GL against Vulkan |
| `tools/fuactl/scenarios/brightmap.mjs` | GLDEFS brightmaps on a wall and a sprite in a dark room, bindless against a binding per material; the brightmap PNG is a lump between `TX_START`/`TX_END` |
| `tools/fuactl/scenarios/stackportal.mjs` | a ceiling that is a stacked sector (`UpperStackLookOnly`/`LowerStackLookOnly` paired by TID) onto a room off the map's side, GL against Vulkan |
| `tools/fuactl/scenarios/stackwalls.mjs` | rays crossing a low stacked annex above its ceiling, which only GL's stack walls catch, GL against Vulkan |
| `tools/fuactl/scenarios/stackslope.mjs` | a steep sloped floor seen through a stacked ceiling, which the stacked view must place by its subsector, not as a wall |
| `tools/fuactl/scenarios/stacksprite.mjs` | things standing where only the stacked view's walk reaches, GL against Vulkan |
| `tools/fuactl/scenarios/stackwide.mjs` | an upper room wider than the stacked room under it, which crashed GL with a stack overflow on load |
| `tools/fuactl/scenarios/linemirror.mjs` | a one-sided `Line_Mirror` wall reflecting the room and the player under GL's sphere-mapped sheen, GL against Vulkan |
| `tools/fuactl/scenarios/animdefs.mjs` | an ANIMDEFS lump whose discrete wall animation flips frame a second after the level is built and then holds, so a backend drawing its bake frame differs from GL in a still shot |
| `tools/fuactl/scenarios/lightlinks.mjs` | a point light 16 units behind a solid wall with a radius reaching the next room, which GL leaves dark because a light's links stop at solid walls |
| `tools/fuactl/scenarios/plainwall.mjs` | a one-sided wall wearing a see-through texture (`MIDGRATE`) with a lit room behind, which GL's solid pass draws without an alpha test; pressed against it (`close`, per pixel), the edge texels GL's `SmoothEdges` colours |
| `tools/fuactl/scenarios/polyobjwall.mjs` | a polyobject (a counter-clockwise template box with `Polyobj_StartLine`, `ENGINE_THING.PolyAnchor` and `PolySpawn`) moved in front of the player, whose u has to span the face |
| `tools/fuactl/scenarios/selfref.mjs` | a raised sector hidden by self-referencing lines (`selfRef`), which GL draws as the surrounding room's floor through the subsector's render sector |
| `tools/fuactl/scenarios/skywall.mjs` | a plain-sky room with a taller room behind its wall, which GL's sky walls (up to 32768) hide |
| `tools/fuactl/scenarios/skyboxseam.mjs` | a six-face GLDEFS skybox (faces between `TX_START`/`TX_END`, `sky1` set by a MAPINFO lump) seen at its corners: the seam and the filter, GL against Vulkan |
| `tools/fuactl/scenarios/translucentmid.mjs` | a two-sided middle texture at half alpha (`block`'s `line(edge)` sets the linedef's `alpha`), GL against Vulkan |
| `tools/fuactl/scenarios/pitchframing.mjs` | a tall sky room level and looking up: the camera's 1.2 pixel stretch, GL against Vulkan |
| `tools/fuactl/scenarios/nogl.mjs` | modelstandalone's voxel box and its two reflections, GL against a `-vulkan` launch (`noGl: true`): it must render them as GL does rather than crash on the model's GL vertex buffer |
| `tools/fuactl/scenarios/viewwindow.mjs` | modelstandalone's room at `screenblocks 8`, GL against Vulkan: the view must sit in the view window above the status bar as GL's `SetViewport` puts it |
| `tools/fuactl/scenarios/scrollwall.mjs` | a scrolling wall over a scrolling floor from one engine frame (`compareSameFrame`): textures stand where GL's do after the scrollers ran |
| `tools/fuactl/scenarios/particles.mjs` | a particle fountain in standalone (`compareCounter`, a floor): the backend must be handed the particles from its own walk |
| `tools/fuactl/scenarios/particlecolor.mjs` | particles' red fountain from one engine frame: Vulkan's particles take the fountain's colour, not the texture's white |
| `tools/fuactl/scenarios/skymove.mjs` | skywall's rooms, spawning away from the sky wall and turning to it, in standalone: the sky above the wall made that frame |
| `tools/fuactl/scenarios/stackturn.mjs` | stackportal's rooms, turning to look up through the stacked ceiling, in standalone: the upper room walked that frame |
| `tools/fuactl/scenarios/skyboxturn.mjs` | a sky yard over a skybox room with a different texture per wall, turning west, in standalone: the skybox walked that frame |
| `tools/fuactl/scenarios/mirrorsprite.mjs` | a line mirror facing pillars behind the player, in standalone: the mirror's own walk each frame |
| `tools/fuactl/scenarios/modelstandalone.mjs` | a voxel model ahead and two seen only in a line mirror, in standalone: solid models drawn as GL draws them |
| `tools/fuactl/scenarios/movedbehind.mjs` | a platform raised while behind the player, in standalone: re-baked when a walk first reaches it |
| `tools/fuactl/scenarios/stencilsprite.mjs` | a stencil-style actor: a flat silhouette in its stencil colour with GL's framed outline (TM_MASK) |
| `tools/fuactl/src/mapdata.mjs` | read a real map's sectors, lines and scripts, to find the ingredient to template |
| `tools/fuactl/src/acs.mjs` | ACS0 BEHAVIOR writer for scripted template maps |
| `tools/fuactl/test/{udmf,template,pngcore}.test.mjs` | their tests |
