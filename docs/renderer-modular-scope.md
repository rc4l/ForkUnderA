# Renderer as a modular monolith, and fast: scope

**Status: active, 2026-10-12.** Builds on [`renderer-modernization-PLAN.md`](renderer-modernization-PLAN.md),
whose policies hold unchanged: GL is the oracle, and Doom's lighting, translucency and mod-facing
semantics are fixed. This document draws the module boundaries and orders the work by what was
measured on 2026-10-12.

## Where we are

| | GL alone | Vulkan standalone |
|---|---|---|
| Sunder MAP10 start, 8,354 walls in view | 6.5-7.1 ms to render | 1.6-1.7 ms |
| AOW03 at (-8944, 12536) yaw 60, 484 walls | 0.7-1.0 ms a frame | about 0.9 ms (1.2-1.3 before the loop-commit fix) |

On the light view Vulkan's GPU time is 0.12 ms, so the whole frame is processor work. A sampler (see
memory `windows-sampling-without-admin`) put it here:

- Committing the 4096-slot bindless binding in TRANSITION mode: 33% of the frame. **Fixed**
  (`fua_dg_loopcommits`, -0.56 ms in six of six rounds).
- Re-baking scrolling, crushing and relighting surfaces, and uploading them again: about 40% of what
  remains. AOW03 has 101 scrolling walls, three scrolling planes, two crushers and many changing
  lights.
- The skybox is a second full pass, and five frame-wide jobs (moved-geometry refresh, light
  collection, bindless refresh, animation re-resolve, the sprite stream) run again in every pass.

Vulkan wins where there is a lot to draw. It loses its lead on a light view because its fixed costs
(the whole level every frame, the second pass, the re-bakes) do not shrink when the view does.

## The target

```
engine: game state, frame loop, 2D
  -> renderer interface      (one header: level load and unload, frame, 2D, camera textures, backend choice)
       -> gl backend         (gl/: Zandronum's renderer, the oracle, knows no module below)
       -> vulkan backend     (features/hwrender: device, pipelines, materials, passes, frame)
            -> scene         (features/scene: surfaces, piece records, dynamic values, per-frame instances, visibility)
            -> materials     (texture identity, animation frames, brightmaps)
```

Rules:

- A module is reached only through the public headers at its folder root. Everything else in it is
  internal.
- No cycles. Scene and materials know no backend, and the two backends do not know each other.
- The engine reaches the modules through the renderer interface, plus the game-state change counters
  already in `r_defs.h`.
- Pure logic lives in `Compute*` units with colocated tests (AGENTS.md).
- Each module's README lists its public API and its engine hooks.

## Where today's code breaks those rules

Inventory of 2026-10-12:

- **139 calls from engine code into the new modules, across 25 files.**
  - 39 hand GL's per-frame walk to Vulkan (capture).
  - 18 have GL using the cache for its own drawing or replay.
  - 11 are standalone switches inside GL.
  - 28 are frame and level lifecycle.
  - 16 are dirty tracking, plus 21 counter bumps in game logic.
- **The backend and the level cache use GL's types:** `FMaterial` 118 times, `GLWall` 80, `GLFlat` 17,
  `GLSprite` 12.
- **`features/levelmesh` calls the GL API itself** (`staticmesh`, `flatmesh`, `modelmesh`).
- **Even standalone Vulkan runs its frame prep inside `FGLRenderer::DrawScene`,** and draws sprites
  through `GLSprite::Process`.
- **`dgscene.cpp` is 8,240 lines.** Eight pass flags (`g_inSkyPortal`, `g_lineMirror`,
  `g_stackPassSlot`, ...) are globals that every pass reads.

### What the backend takes from `gl/`, by kind

There are 129 distinct GL symbols in all. Only one item is genuinely GL-specific: the VBO, VAO and IBO
code in `staticmesh.cpp`, which serves GL's own mesh path. Everything else can be derived from game
state, but lives in GL's code today. This is the checklist for P3 and P4.

| Kind | Arrives today through | Moves to |
|---|---|---|
| Light model | `CaptureShading` calls `gl_SetColor`/`gl_SetFog` and reads back `gl_RenderState`; `gl_ClampLight`, `getExtraLight` | a neutral lighting unit in scene, ported as `Compute*`; GL keeps its own |
| Materials | `FMaterial` (138 references), `FTexCoordInfo`, `gl_info` brightmaps and shader speed, GL translations | the materials module, over `FTexture` and GLDEFS |
| Vertex and planes | `FFlatVertex`, `GLSectorPlane`, `gl_FakeFlat`/`area_t` | scene types |
| Walls | owned segs are map-baked; declined segs and the first fill come from GL's walk (`ArmFullBake`, `RecordPiece`) | the map bake covers every seg; `TryReplay`/`StaticWall` move into `gl/`, since they serve GL |
| Flats | map-baked; `RegisterFlatSubsector` stands down under the map bake | scene |
| Sprites | `GLSprite::Process` and `Draw`, even in standalone; actor light from `gl_SetDynSpriteLight` | a neutral sprite derivation, checked against GL |
| Models | GL's loaders and `FModelVertexBuffer`, captured at draw | split model data from GL's buffers (the most entangled) |
| Decals | glued quads from `GLWall::DrawDecal`; projected decals need only texture sizes | projected only (modernization plan, Phase 3) |
| Dynamic lights | `ADynamicLight` and its links live in `gl/dynlights` | read through a neutral accessor |
| Sky and portals | sky quads and holes, mirror and stack visibility come from GL's walks; `mSky1Pos`, `SetSkyFog` | visibility from the map (portal flood) and sky values from game state |
| 2D | GL's `DrawTexture`/`Dim`/`Clear` record quads | record at the canvas layer, below any backend |
| Camera | `SetMainViewLens` from GL's `RenderViewpoint`; camera textures triggered from GL | computed from game state, behind the renderer interface |

## Phases

Each phase ships on its own. It is measured with `fuactl abperf` in one session and attributed with
the sampler, and the scenarios stay green.

### P1: speed without moving anything (days)

1. Loop-commit fix. Done.
2. Frame-wide jobs once per frame, not once per pass.
3. Scroll offsets in the piece record, so a scrolling wall or plane updates a record and not geometry.
4. Light-only changes never re-derive geometry. A seg that does not own its sidedef is skipped
   cheaply.
5. The skybox pass keeps its batch list instead of testing the whole level each pass.

**Accepts when:** Vulkan at the AOW03 view is at or under GL alone, Sunder MAP10 does not regress, and
the p99 of a frame that runs a tic is halved.

**Status, 2026-10-12.** Each item was measured with `abperf` in one session at the AOW03 view.

| Item | Switch | Result |
|---|---|---|
| Loop commits | `fua_dg_loopcommits` | -0.56 ms, faster in all 6 rounds |
| Frame-wide jobs once per frame | `fua_dg_frameprep` | no measurable change; kept for structure, and because the sprite stream was being sorted from the skybox camera |
| Scroll in place, walls and planes | `fua_mesh_rescroll` | -0.27 ms, faster in all 6 rounds; scenario `scrollwall` (new `compareSameFrame` mode) |
| Relight flats without rebuilding; owner check first | none | invalidation loop 0.32 to 0.20 ms a frame |

The median frame went from 1.24-1.48 ms to 0.77 ms. That is at or under GL alone's 0.7-1.0 ms, so the
first criterion is met. The p99 went from about 2.9 to 2.3-2.45 ms, which is not halved. What is left
on a tic frame is planes that move: AOW03's crushers interpolate every frame, so their walls and
flats are rebuilt every frame. The fix is plane heights read by the shader from a per-sector table,
which belongs to P3. At this view the frame now also waits in `AcquireNextImage` (28%), which is
presentation and not the renderer. The skybox batch-list item has not been done.

### P2: the backend's own structure (no behaviour change)

- Split `dgscene.cpp` by responsibility: device and pipelines, materials and bindless, scene upload and
  patching, frame prep, world pass, blended pass, sky, mirrors, stacked views, lights, 2D, frame
  driver, diagnostics.
- A `PassContext` replaces the pass globals.

**Accepts when:** no file is over about 1,500 lines, a Vulkan frame is identical (0.0%) before and
after, and the scenarios are green.

**Status, 2026-10-12.** `dgscene.cpp` went from 8,240 to 5,766 lines.

| Moved to | Lines | What |
|---|---|---|
| `dg2d.cpp` | 326 | the 2D pass |
| `dglights.cpp` | 416 | the light list, light links and cluster grid, behind seven calls |
| `dgsky.cpp` | 1,324 | the dome, skybox, portal, fade, sky surfaces and occluders, behind about a dozen calls |
| `dgscene_glsl.h` | 512 | the world pass's GLSL |

`PassContext` replaced the eight pass globals. `dgscene_internal.h` carries what the backend's files
share.

The mirrors and stacked views, the next 1,190 lines, would import 47 names, mostly the scene's own
data: batches, vertex and piece buffers, depth targets, stack visibility and sky holes. Splitting them
now would only turn those into extern globals. They wait for P3's scene object, so they can be split
against its API instead.

**Finished in P3.** The shared state became four records in `dgscene_internal.h`: `g_scene`
(buffers, batches, dynamic runs), `g_pso` (the world pipelines), `g_frame` and `g_stats`. The rest of
`dgscene.cpp` then split along responsibility, and no file is over 1,310 lines.

| File | Lines | What |
|---|---|---|
| `dgscene.cpp` | 894 | the world pass, frame prep, scene depth, `DrawSceneOnce` |
| `dgpipelines.cpp` | 908 | pipelines and material bindings |
| `dgupload.cpp` | 919 | the scene buffer and its patches |
| `dgdynamic.cpp` | 595 | the per-frame stream and the translucent pass |
| `dgviews.cpp` | 1,150 | line mirrors, plane mirrors, stacked views |
| `dgcamera.cpp` | 157 | camera textures |
| `dgframe.cpp` | 334 | the frame driver |
| `dgdiag.cpp` | 543 | benches, statistics, readbacks |

Against the pre-split binary, Vulkan frames of 13 template scenes were identical to the pixel, apart
from content that varies between two runs of the same binary (animated textures, console text).

### P3: a scene module and a materials module

- Move the map-derived surfaces, the piece registry and its arena, dirty tracking and the per-piece
  dynamic values into `features/scene`, with neutral types.
- GL buffer code leaves it. `fua_gl_meshdraw` and GL's indexed mesh path are deleted.
- A materials module gives texture identity, animation frames and brightmaps to the backend in place
  of `FMaterial`.

**Accepts when:** scene and materials include nothing from `gl/` or Diligent, and their logic is
unit-tested.

**Status, 2026-10-12.**

| Item | Result |
|---|---|
| GL's mesh path | Deleted: `fua_gl_meshdraw`, the indexed path, `fua_gl_meshbench`, `StaticWallRange`. `staticmesh.cpp` makes no GL call. |
| The backend's own structure | P2's split finished against four records (`g_scene`, `g_pso`, `g_frame`, `g_stats`); see the table under P2. |
| Light model | `ComputeShading` (`levelmesh/computation/shading_compute`) is `gl_SetColor` and `gl_SetFog` as pure functions, 100% covered. GL publishes its settings through `gl_ShadingEnv`. `fua_shading_check` compares every shaded surface bit for bit against GL's setters: 0 differences in about 28.5 million surfaces on AOW03, wdiex02, WDI01, WDI09, WDI18 and dbab04, including software lighting, bright fog and a fixed colormap. |
| Materials | `features/materials` gives the backend texture identity, the frame an animated texture shows, brightmaps, pixels and size. No backend file includes `gl_material.h`. |
| Vertex | `MeshVertex` replaces GL's `FFlatVertex` in the scene, backend, horizon and surfaces; `glvertex.h` asserts the shared layout where GL hands vertices over. |
| A crash found on the way | A Vulkan frame after a level change, before GL's next walk, drew the skybox from the old level's freed `SkyViewpoint` (access violation; wdiex02 reloaded with a `goto` straight after: 7 deaths in 8 across the old and new binaries). The level's walk captures are now dropped at unload (`ForgetWalks`): 5 of 5 survive. |

`staticmesh`, the light model, the vertex type and every `Compute*` unit include nothing from `gl/`.
Frames of 15 template scenes are pixel-identical to the pre-P3 binary.

Moved to P4, because each is the same work as taking Vulkan off GL's walk:

- **The map bake out of `wallcache.cpp`.** `wallcache.cpp` holds GL's own replay cache, the capture from GL's walk and the map bake together. The bake (`surfaces/surfacebuild.cpp`) still reads `GLWall`'s part numbering, `FTexCoordInfo` and `GLSectorPlane`.
- **Tic frames.** At the AOW03 view a frame that runs a game tic costs 2.4 ms against 0.75. The tic itself is 0.52 ms. The renderer's extra is about 1.1 ms: 70 sectors invalidated per tic (light changes and moving planes in about equal numbers), about 500 surfaces repainted and 60 extra uploads. The fix is sector light and plane heights in a table the shader reads, which is light taken from game state, P4's job.

### P4: Vulkan without GL's walk

- Derive per-frame instances from game state, not from GL's capture:
  - sprites, through a neutral port of the sprite derivation, checked against GL as the oracle;
  - models, decals and dynamic lights;
  - sky, skybox, portals, mirrors and stacked views.
- Light and plane heights from game state: a per-sector table the shader reads, so a light change or a
  moving plane updates a table entry instead of repainting pieces (from P3).
- The map bake out of `wallcache.cpp` into the scene, with its own part, texture-coordinate and plane
  types; GL's replay cache moves into `gl/` (from P3).
- Delete the capture hooks and standalone switches from `gl/`. GL goes back to being Zandronum's
  renderer plus its own wall cache.
- Side by side becomes two independent renderers on one game state.

**Accepts when:** `gl/` calls no module except through the renderer interface, the scenarios are
green, and GL's frame rate side by side equals GL alone.

**Status, 2026-10-12.** Standalone (`fua_dg_standalone 1`, the mode a player runs) already skipped GL's
walk for walls and flats. The template harness turns standalone off for its Vulkan sides, so the suite
had been testing Vulkan with GL walking. Run with standalone on, 41 of 42 GL-against-Vulkan scenarios
passed; the exception was `stacksprite`. All 42 pass now.

| Item | Result |
|---|---|
| Sprites | `features/sprites` derives an actor's sprite as `GLSprite::Process`, `Draw` and `RegisterSprite` do: 31 unit tests, and `fua_sprite_check` matched GL bit for bit on about 1.6 million actor-frames, rare settings included. On by default (`fua_sprites_own`); the sprite section costs the same as GL's (0.34-0.36 ms against 0.35-0.38 on wdiex02). Models are derived too (`modelbuild`, `modelpose_compute`): standalone had drawn no solid model, since only `RenderScene` draws GL's model list, and none in stacked views or mirrors (`modelstandalone`). GL's model path shares the pose and frame blend; the check matched about 103,000 model surfaces bit for bit. |
| Visibility | `features/visibility` walks the player's view as GL's BSP walk and clipper do, frustum seed included. The player's pass draws only what the walk reached (the walked-bit cull stacked views already used), the visible-set test `skybleed-scope.md` found the map bake lacked. On by default (`fua_vis_walk`): the sampler puts the walk at about 2.4% of a wdiex02 frame, 0.04 ms. |
| Sky surfaces | The walk makes the player's sky surfaces, sky holes and skybox each frame, from `surfaces/computation/skywall_compute` (`SkyNormal`, `SkyTop`, `SkyBottom`, `DoHorizon`). `fua_vis_check` matched GL bit for bit on about 19,000 frames across nine maps, horizons on and off. |
| Stacked views and skyboxes | The walk collects each stacked plane's walls and covered subsectors (GL's flats pass and `ProcessSectorStacks`) and each skybox its sky walls name; each is then walked from its own eye with GL's portal clipper (`ClearClipper`, `SetupCoverage`), giving its walk bits, sky surfaces and sprites. Matched GL on about 6,900 frames across seven maps, apart from views GL's occlusion query skipped. |
| Mirrors and particles | The walk notes each `Line_Mirror` wall, its sheen light (the band `SplitWall` gives its first piece, `mirrorview_compute`) and walks the reflection for its sprites; 2,220 mirrors and sheens matched GL. It hands each reached subsector's particles to GL's sprite path, which standalone had never done. |

Found on the way: standalone's sky surfaces, skybox, stacked views, skybox walk and mirrors were
whatever GL's walk captured before standalone took over, stale ever after (on a map where nothing
moves, the spawn view's), and it drew no actors in mirrors and no particles at all. Each now comes
from the walks every frame; scenarios `skymove`, `stackturn`, `skyboxturn`, `mirrorsprite` and
`particles` fail on the old binary and pass now.

Then the capture went. `features/frame`'s frame driver is the engine's renderer: it sets each frame
up once (`BeginFrame`, `BeginPlayerFrame`, the lens in `r_viewLens`, the weapon bob once a frame by
`r_frameSerial`) and runs GL's view and the backend's each when it is drawn, so neither drives the
other. GL's walk fills nothing of the backend's any more. The map bake is the only bake
(`fua_surface_mapbake_auto` is gone), level set-up and teardown are `FLevelRenderer`'s, and GL's
replay cache lives in `gl/scene/gl_wallcache.cpp`. GL reports its walk and draw to an observer it owns
(`gl_observer.h`) only while a check against GL is on, and `features/sidebyside` turns that into the
checks. The last two things only GL's capture drew are derived too: the decals glued to walls
(`levelmesh/walldecals`, scenario `walldecal`) and the sky's fog (`skyfog_compute`, scenario `skyfog`).
Both were missing from standalone and `-vulkan` before. The harness runs Vulkan sides standalone, as a
player does.

`gl/` now reaches modules only for shared rules (`Compute*`), GL's own sprite view, and the frame
modules both renderers use (`frame`, `viewsetup`, `draw2d`, `display`, `replay`).

Still to do: models' data without GL's buffers (the loaders and `FModelVertexBuffer` are GL's); GL's
light settings the modules read (`glset`, `gl_fixedcolormap`, `getExtraLight`, `gl_ClampLight`), which
want a neutral owner; and light and plane heights from game state (on wdiex02, re-baking what moved or
relit is about 30% of a standalone frame).

### P5: one renderer interface, Vulkan owning itself

- The engine selects a backend at startup.
- Vulkan creates its own window and device when GL is not running, so GL becomes optional at runtime.

**Accepts when:** a Vulkan-only launch creates no GL context.

**Status, 2026-10-12.** What GL still owns in a standalone frame was mapped: the window and context,
every 2D quad (GL draws each one and records it), the view setup in `RenderViewpoint`, wipes,
gamma, screenshots and save pictures, and materials (`FMaterial`). First step: `features/draw2d`
works out a 2D draw once, without GL (`DrawTexture`'s quad and texture mode, lines, pixels, dims,
clears), and GL draws and records that; GL and Vulkan frames of the HUD, console and menu are
unchanged. Then `features/viewsetup` works out the view once (the stretched pitch, the roll, the
actor the eye is in, the walks' frustum), which GL's `RenderViewpoint` and the walks read. The screen's
tint (`draw2d/screenblend`) and the weapon (`sprites/pspritebuild`) are worked out without GL too, the
weapon checked against GL's record on about 54,000 frames. `features/frame` drives the standalone
scene, GL hooked in only for what the backend leaves it. The walks and the backend no longer read
`GLRenderer`: the sky scroll, the view vector and angles, the view area and the particle and mirror
textures come from `viewsetup` and `materials`, and `InstallSpriteView` gives a frame without GL its
sprite view.

`-vulkan` now launches with no GL context. `I_CreateRenderer` installs the backend's renderer, whose
`RenderView` is `features/frame`'s `RenderPlayerView`. `Win32GLVideo` keeps the window and modes, sets
no pixel format, and makes the backend's frame buffer: 2D recorded through `features/draw2d`, palette
and gamma from `features/display`, the window's size and scaling reconciled as GL's are, and
`screenshot` read back from the swapchain. The title, menus, console and intermission draw over black;
a level draws once the map bake is up. Without GL, model vertex buffers keep only their CPU copy, and
texture reads and flushes no longer go through `GLRenderer`. Two fixes reach the GL launch's Vulkan
view too: the view window (a status bar, a smaller `screenblocks`) frames the world as GL's
`SetViewport` does (scenario `viewwindow`), and `FlatFill` (border, status-bar flanks, text screens)
is recorded. Checked at 16 spots on MAP01, wdiex02, WDI01, WDI07 and WDI18: the `-vulkan`
frame is within 0.3% of the GL launch's standalone frame, apart from HUD text and a randomised item
that differed between the two launches (scenario
`nogl`).

The rest followed. `FillSimplePoly` (the textured automap's floors) is worked out once in
`draw2d` (`ComputePolyTexCoords`, `ComputePolyTriangles`) and drawn as GL's fan by both. The wipes are
`features/wipe` (melt, crossfade, burn as `Compute*`, GL's `gl_wipe.cpp` reading them too) and the
backend draws them from the last frame and the new one. The save picture is the player's view rendered
at the picture's size with the screen's blend applied (`ComputeApplyScreenFill`). The internal render
scale is an offscreen frame target at the screen's size, drawn up to the window at present as GL's
scale buffer is (`vid_scalefactor 0.5` gives 512x320 shown in the window). HUD models are posed as
`gl_RenderHUDModel` poses them and drawn over the world (scenario `hudmodel`: beside GL, standalone and
without GL the box covers the 20,986 pixels GL's does, to one).

### P6: visibility and threads

- Cost tracks what is visible: sector or portal visibility, or GPU culling into indirect draws.
- Per-frame instances and patches are built on a worker thread, and passes are recorded in parallel
  with deferred contexts.

**Accepts when:** the modernization plan's Phase 2 target is met (Sunder MAP10 CPU under 1.5 ms and
GPU-bound), and Vulkan is clearly ahead of GL at the AOW03 view.

**Status, 2026-10-12.** Two costs on wdiex02 in standalone, found with a stack sampler:

| Item | Result |
|---|---|
| Light links per tic | `CollectDynamicLights` walked and sorted all 767 lights' touching lists each tic, 4.7 ms on every tic frame. Each light's sorted links are now kept until it moves or changes radius (none stale in 600,000 checks). |
| Re-bakes that track what is visible | Every frame a sector moved or relit, its walls and flats were re-baked, seen or not: about 0.78 ms a frame there. Now only what a walk reached this frame is re-baked, and the rest waits until a walk reaches it (scenario `movedbehind`). The loop costs 0.001 ms at that view; with the walks against without, p50 is 0.4 ms faster in every round and p99 goes from 6-7.5 ms to about 3.2 ms. |
| Against the acceptance views, `-vulkan` (1024x640, `fuactl capture`, three rounds) | AOW03 at spot q36: GL alone 1.36-1.50 ms p50, `-vulkan` 0.57-0.60 ms, so Vulkan is clearly ahead. Sunder MAP10 at spawn: GL alone 6.04-6.07 ms, `-vulkan` 1.85-1.97 ms, waiting about 22% of the frame in `AcquireNextImage`. The main thread is busy about 1.6 ms a frame against the 1.5 ms target, and the player's walk is 1.37-1.48 ms of it: 7,411 of the level's 10,958 subsectors and 23,366 segs, with no single hot line. |
| The sliced walk (`features/jobs`, `features/visibility`) | The player's walk runs in one slice a lane, each clipper blocked outside its share of the view, the union exactly one walk (leaky node boxes included). Sunder MAP10: the walk from about 1.15 to 0.42 ms, the frame p50 from 1.58 to 0.79-0.82 ms, under the 1.5 ms target. The GPU takes 0.24 ms of it (`fua_diligent_bench`), so the frame is CPU-bound by design now: culling cut the GPU's work faster than the CPU's. |
| Dynamic instances on workers | A frame's sprites, models and decals are laid out on the game thread and written on the job pool: `BuildDynamic` 0.13 to 0.09 ms on Sunder MAP10 with the monsters awake. The actors' sprites are derived in chunks on the pool too. |

## Decisions taken

- GL stays as the oracle, as the modernization plan says. After P5 it is optional at runtime.
  Removing it outright would need another oracle, such as stored reference frames.
- The GL-draws-from-mesh path exists only for A/B, and goes in P3.

## Measuring

- Views:
  - AOW03 at `--at -8944,12536,152 --face 60,0` (light);
  - the `q42-gl-draws-plain-with-vulkan` spot;
  - Sunder MAP10 start (heavy).
- Tools: `fuactl abperf` for in-session A/B, the sampler for attribution, `capture` for p50 and p99,
  and the template scenarios for the picture.
