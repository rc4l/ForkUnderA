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

Worked example: `tools/fuactl/scenarios/horizon.mjs` (about 45 lines).

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
the boxes that did not pass and exits 1 if any scenario failed. It runs every Vulkan side standalone,
as players do (`fua_dg_standalone 1`, except `frame: "gl"` and `glDraws` sides), so `scenarios
--standalone` adds nothing beyond a narrower set.

`compareTemplate` refuses to run while another engine is open (GL readback under another window is
undefined), launches each map once per side — GL alone, then Vulkan with `fua_dg_standalone 1` (0 only for a side
judged by GL's frame) — captures
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
checks it still draws after the level loads again, and `glplain.mjs` checks it never reads the mesh.
A side's `launchCvars` go on the command line, for what must hold from the level's upload, where its
`cvars` are set once the level is up (`cameratexpermaterial.mjs`: per-material bindings are made at the
upload). A ray-traced side needs `commands: ["fua_dg_rtmirrors 1", "fua_diligent_scene"]`, since tracing
is chosen when the scene is uploaded, and the scenario `needs: ["raytracing"]` (`rtsprites.mjs`). `atVantage` commands run once it stands at each vantage, for a change the view's own walk must see first
(`fua_mesh_blank` empties the mesh the map bake filled). `fuactl scenarios <name> --side S --cvar k=v`
adds cvars to one side, which is how a test shows it fails under the behaviour it guards against; a
scenario without that side is skipped rather than ending the run. `standalonemove.mjs` runs its script
after the level is up, so the move is not baked in. A side that spawns facing away and then turns
(`skymove`, `stackturn`, `skyboxturn`) catches a frame that reuses an earlier walk instead of walking itself.

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
cvars. `spriteslots.mjs` reads `fua_dg_dynstats`; `particles.mjs` holds a floor. `compareOverTime` and
`compareCounter` both launch with the first non-GL side's settings.

For what a command must CHANGE and what must SURVIVE one (a mark made, then a resize), use
`compareSequence`: `steps` are captures taken in turn, each after its `commands` (console lines, a number
of ms to wait, or a camera to stand at), and each box names the `pair` of captures it is judged
between. `decalresize.mjs` makes a mark, changes a setting from one spot and judges it from another,
since a stale depth binding matches only the view it was frozen in. Cheats are toggles and a locked
launch already has `god` on, so a scenario asking for `god` turns it off.

For a fault on the engine's way out, use `compareQuit`: each side is launched, plays `settleTics`, and is
quit from its console as a player quits; a crash file beside its log, an engine still running or an exit
code other than 0 fails it (`judgeQuit`). `quitclean.mjs` quits the backend with Diligent's messages on.

For a leak, which no frame shows either, use `compareSoak`: each side repeats the scenario's `action` (a
reload unless it names console steps, or a function of the launch's `{ wadPath, iwad }` that builds them)
`cycles` times and passes when the engine's soak verdict says no memory, object, handle or thread count
kept growing past `warmup` (`judgeSoak`), leaving out readings under each `ignore` prefix. `soak.mjs` reloads
a map with lights and monsters; `wadsoak.mjs` and `wadsoaktitle.mjs` swap WAD sets with `wad_reload`.

`scenarios --webgpu` runs the set on the WebGPU device, and how depends on how a scenario judges: one
compared across launches runs its first Vulkan side on Vulkan and on WebGPU, every box at
`WEBGPU_CEILING`; one judged against itself (a comparison marked `judgedAlone` in `template.mjs`) runs on WebGPU alone
with its own limits; a `compareSameFrame` one (a WebGPU launch has no GL) and one that `needs:
["raytracing"]` are skipped with that reason. A scenario that throws counts as one failure and the run
goes on.

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
| `tools/fuactl/src/template.mjs` | `compareTemplate` (launch, capture, judge), the other `compare*` comparisons, and the pure `judge*` verdicts |
| `tools/fuactl/src/pngcore.mjs` | PNG decode/encode, `boxDiff`; `fuactl png --boxdiff` on the CLI |
| `tools/fuactl/scenarios/horizon.mjs` | the worked example |

Every scenario opens with a one-line `// [rc4l]` summary of what it proves, so the list is generated
rather than kept here: `head -qn1 tools/fuactl/scenarios/*.mjs`.
| `tools/fuactl/src/mapdata.mjs` | read a real map's sectors, lines and scripts, to find the ingredient to template |
| `tools/fuactl/src/acs.mjs` | ACS0 BEHAVIOR writer for scripted template maps |
| `tools/fuactl/test/{udmf,template,pngcore}.test.mjs` | their tests |
