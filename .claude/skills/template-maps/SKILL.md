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

const m = new UdmfMap();
// A closed room, walked clockwise so it is on every wall's right; walls(edge) styles each edge.
m.room(0, 0, 1024, 1024,
  { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
  (edge) => edge === "north" ? { side: { texturemiddle: "STARTAN2" }, line: { special: 9 } }
                             : { side: { texturemiddle: "BROWN1" } });
m.thing({ x: 512, y: 724, angle: 90, type: 1 });          // player 1 start
const bytes = wad([...mapLumps("MAP01", m), ...mapLumps("MAP02", other)]);
```

- `room()` for closed sectors, `block()` for a sector standing inside another (two-sided lines, e.g.
  a pool or a tagged area), `skyboxRoom(x, y)` for a ready skybox every sky ceiling shows, `wall()`
  for single one-sided lines (sector on the right), `sector()`, `thing()` (present on every skill and
  mode unless told otherwise), `vertex()` shares corners.
- **Behaviour set by ACS** (reflective floors, anything only a script can do): `src/acs.mjs` writes an
  ACS0 BEHAVIOR lump with one OPEN script -- `acs0([...callSpecial(159, [tag, 66, 0]), PCD.TERMINATE])`
  -- passed as `mapLumps("MAP01", m, { behavior })`. No ACS compiler is needed.
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
each box's verdict, and exits 0 (all under ceiling), 1 (failed), 2 (crashed) or 3 (refused) -- so a test
can import the builder without launching anything. The whole set runs as one command,
`node src/cli.mjs scenarios [name ...] [--engine build-win/Release/forkundera.exe]`, which prints only
the boxes that did not pass and exits 1 if any scenario failed.

`compareTemplate` refuses to run while another engine is open (GL readback under another window is
undefined), launches each map twice — GL alone, then Vulkan with `fua_dg_standalone 0` — captures
every vantage, and judges each box by `boxDiff`: mean |d| in 16-pixel block means, so the renderers'
sub-pixel framing averages out while a block-sized fault stays loud. Its GL frames come from the
Vulkan-off launch on purpose: with Vulkan on, GL captures on a skybox map were unreliable in every build.
Every frame it judges is kept, as `<engine dir>/sweep/<name>_<map>_<vantage>_<side>.png`, so a failing box
can be looked at rather than re-shot by hand. "Another engine" includes the reporter's own side-by-side session: quit it by its token before a GL-side
run and relaunch it afterwards (`fua-side-by-side`), never by process name.

When the fault is a Vulkan switch rather than a GL difference, pass `sides`: two named launches, each
with the cvars it sets and any `commands` to run after them (`fua_mapbake_now` re-bakes with a bake
cvar changed), compared Vulkan against Vulkan. `scenarios/pointlight.mjs` compares
`fua_dg_clusters 0` against `1`, which match exactly once fixed, so its ceiling is 0.05. With no GL side
nothing is read back from a window, and the harness runs even with other engines open.

A box can name a `vantage`, when the vantages look at different parts of the frame. A model needs no
assets: `kvxBox` and `voxelLumps` in `src/kvx.mjs` put a KVX voxel on every frame of a sprite.

A side's `commands` can also load another map of the same wad (`map MAP02`), which makes the oracle a
level that is right by construction: `waterseam.mjs` judges a pool split into two sectors against the
same pool in one piece, and `underwatersprite.mjs` a medkit under all-but-opaque water against the
empty pool.

GL and Vulkan do not frame a view identically yet: GL applies Doom's 1.2 pixel stretch vertically and
Vulkan does not, so a thin GL-vs-Vulkan box over a few rows of geometry reads several points of
difference on a correct build. Prefer a Vulkan-vs-Vulkan oracle that is right by construction for the
region (`water3d.mjs`: bands off is the wall's own light, which is what the part above the water must
have), and prove it fails on a deliberately broken build.

For something that must MOVE (a warp, a scroller), use `compareOverTime` instead: one launch, each
vantage captured twice `intervalMs` apart, and a box can carry a `floor` (it must change at least this
much) as well as a `ceiling`. Judge it per pixel (`block: 1`): a ripple shifts an even texture without
moving its 16-pixel block means.

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
| `tools/fuactl/src/template.mjs` | `compareTemplate` (launch, capture, judge) and the pure `judge` |
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
| `tools/fuactl/scenarios/brightmap.mjs` | GLDEFS brightmaps on a wall and a sprite in a dark room, bindless against a binding per material; the brightmap PNG is a lump between `TX_START`/`TX_END` |
| `tools/fuactl/scenarios/stackportal.mjs` | a ceiling that is a stacked sector (`UpperStackLookOnly`/`LowerStackLookOnly` paired by TID) onto a room off the map's side, GL against Vulkan |
| `tools/fuactl/scenarios/stackwalls.mjs` | rays crossing a low stacked annex above its ceiling, which only GL's stack walls catch, GL against Vulkan |
| `tools/fuactl/scenarios/linemirror.mjs` | a one-sided `Line_Mirror` wall reflecting the room and the player, GL (sheen off) against Vulkan |
| `tools/fuactl/scenarios/animdefs.mjs` | an ANIMDEFS lump whose discrete wall animation flips frame a second after the level is built and then holds, so a backend drawing its bake frame differs from GL in a still shot |
| `tools/fuactl/scenarios/lightlinks.mjs` | a point light 16 units behind a solid wall with a radius reaching the next room, which GL leaves dark because a light's links stop at solid walls |
| `tools/fuactl/scenarios/plainwall.mjs` | a one-sided wall wearing a see-through texture (`MIDGRATE`) with a lit room behind, which GL's solid pass draws without an alpha test |
| `tools/fuactl/scenarios/polyobjwall.mjs` | a polyobject (a counter-clockwise template box with `Polyobj_StartLine`, `ENGINE_THING.PolyAnchor` and `PolySpawn`) moved in front of the player, whose u has to span the face |
| `tools/fuactl/scenarios/selfref.mjs` | a raised sector hidden by self-referencing lines (`selfRef`), which GL draws as the surrounding room's floor through the subsector's render sector |
| `tools/fuactl/scenarios/skywall.mjs` | a plain-sky room with a taller room behind its wall, which GL's sky walls (up to 32768) hide |
| `tools/fuactl/scenarios/skyboxseam.mjs` | a six-face GLDEFS skybox (faces between `TX_START`/`TX_END`, `sky1` set by a MAPINFO lump) seen at its corners: the seam and the filter, GL against Vulkan |
| `tools/fuactl/scenarios/translucentmid.mjs` | a two-sided middle texture at half alpha (`block`'s `line(edge)` sets the linedef's `alpha`), GL against Vulkan |
| `tools/fuactl/scenarios/pitchframing.mjs` | a tall sky room level and looking up: the camera's 1.2 pixel stretch, GL against Vulkan |
| `tools/fuactl/src/mapdata.mjs` | read a real map's sectors, lines and scripts, to find the ingredient to template |
| `tools/fuactl/src/acs.mjs` | ACS0 BEHAVIOR writer for scripted template maps |
| `tools/fuactl/test/{udmf,template,pngcore}.test.mjs` | their tests |
