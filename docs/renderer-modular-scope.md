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

### P3: a scene module and a materials module

- Move the map-derived surfaces, the piece registry and its arena, dirty tracking and the per-piece
  dynamic values into `features/scene`, with neutral types.
- GL buffer code leaves it. `fua_gl_meshdraw` and GL's indexed mesh path are deleted.
- A materials module gives texture identity, animation frames and brightmaps to the backend in place
  of `FMaterial`.

**Accepts when:** scene and materials include nothing from `gl/` or Diligent, and their logic is
unit-tested.

### P4: Vulkan without GL's walk

- Derive per-frame instances from game state, not from GL's capture:
  - sprites, through a neutral port of the sprite derivation, checked against GL as the oracle;
  - models, decals and dynamic lights;
  - sky, skybox, portals, mirrors and stacked views.
- Delete the capture hooks and standalone switches from `gl/`. GL goes back to being Zandronum's
  renderer plus its own wall cache.
- Side by side becomes two independent renderers on one game state.

**Accepts when:** `gl/` calls no module except through the renderer interface, the scenarios are
green, and GL's frame rate side by side equals GL alone.

### P5: one renderer interface, Vulkan owning itself

- The engine selects a backend at startup.
- Vulkan creates its own window and device when GL is not running, so GL becomes optional at runtime.

**Accepts when:** a Vulkan-only launch creates no GL context.

### P6: visibility and threads

- Cost tracks what is visible: sector or portal visibility, or GPU culling into indirect draws.
- Per-frame instances and patches are built on a worker thread, and passes are recorded in parallel
  with deferred contexts.

**Accepts when:** the modernization plan's Phase 2 target is met (Sunder MAP10 CPU under 1.5 ms and
GPU-bound), and Vulkan is clearly ahead of GL at the AOW03 view.

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
