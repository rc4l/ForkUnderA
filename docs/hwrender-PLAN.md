# features/hwrender — modern GL upgrade (staged plan)

## Context

ZandroX is OpenGL-only ([[renderer-gl-only]]). Its renderer is ~2013 GZDoom 1.8: a **GLSL 1.20
compatibility-profile shader path grafted onto the fixed-function pipeline**. Three problems:

1. **The macOS rendering artifact.** Floors/ceilings show the subsector triangle-fan tessellation as
   visible triangular facets/holes. Confirmed at runtime; present in the pre-GPL build too (not
   caused by the GPL work), and absent in base Zandronum's x86 build. Flats are drawn as
   `glBegin(GL_TRIANGLE_FAN)` per subsector with constant `glVertexAttrib*f` attributes interleaved —
   exactly the submission path this upgrade replaces with explicit per-vertex buffer data.
2. **Apple caps this renderer at GL 2.1.** The engine talks the **SDL 1.2 API**, which cannot request
   a GL version/profile at all, so macOS silently gets 2.1 compat.
3. **Long-standing window/video bugs**: no borderless-windowed option; display gamma not restored on
   abrupt exit; every resolution/fullscreen change destroys and recreates the GL context.

**Goal:** a clean upgrade to a modern, Core-profile-capable renderer behind a backend-agnostic seam
(so Vulkan/Metal is later a drop-in), fixing the above — **without touching the fixed-point sim,
DECORATE, or the netcode**. Explicitly NOT porting UZDoom's renderer: it requires modern GZDoom's
float sim + ZScript + FGameTexture, which would break deterministic netplay.

## Current state (done, committed, verified)

- **8 `computation/` units at 100% coverage** (90 tests): `matrix`, `gammaramp`, `vertexconvert`,
  `frustum`, `vertexformat`, `batch`, `renderstate`, `quad`.
- **`irenderbackend.h`** — the backend-agnostic seam; **`gl_backend.cpp`** — Core-profile GL
  implementation; **`renderer2d.cpp`** — 2D draw path; **`shaders.h`** — GLSL 330 base shaders.
  All compile and link into the engine; dormant (nothing calls `CreateGLBackend()` yet).
- VAO + core shader-status entry points loaded in `gl/system/gl_interface.cpp`.

## Key findings that set the ordering

- **VBO + `glVertexAttribPointer` + GLSL 1.20 shaders all work on the CURRENT GL 2.1 context.** Only
  **VAOs** (GL 3.0) and *removing* fixed-function require Core. So geometry can be migrated and
  visually verified on macOS today, and the Core flip comes last.
- **SDL 1.2 is the hard blocker for Core** — but we already build and bundle **SDL2 2.30.10**; the
  engine just reaches it through the `sdl12-compat` shim. Migrating to the native SDL2 API needs no
  new dependency and simultaneously fixes borderless, in-place resize, and Linux vsync.
- **`FRenderState` (`gl/renderer/gl_renderstate.{h,cpp}`) is a genuine "set state → Apply() → draw"
  abstraction** and is the single best retargeting point. `FShader`/`FShaderContainer`/
  `FShaderManager` (`gl/shaders/gl_shader.cpp`) are reusable as-is: `Process()`-injection shader
  composition, the 16-way `#define` permutation matrix, `glBindAttribLocation` slots, uniform caching.
- **Only sector flats live in a VBO** (`FFlatVertexBuffer`), and even it binds with Core-removed
  client arrays (`glVertexPointer`). `glVertexAttribPointer` and `glGenVertexArrays` are loaded but
  **never called** anywhere.
- `FLightBuffer` (`gl/dynlights/gl_lightbuffer.cpp`) is already Core-clean TBO code, just unwired.

**Scale of the legacy surface:** 74 `glBegin` blocks, 110 `glVertex3f`, 132 `glTexCoord2f`, 49
`glTexEnv*` (40 of them in `gl_SetTextureMode`), 32 `glMatrixMode` + ~75 matrix-stack calls, 25
`glColor4f`, 17 `GL_POLYGON`, 5 `GL_QUADS`, 8 `glAlphaFunc`, 3 `glClipPlane`, client arrays in 7
places. All 18 GLSL lumps use removed built-ins (`gl_Vertex`, `gl_ModelViewProjectionMatrix`,
`gl_TextureMatrix[7]`, `gl_FragColor`, `attribute`/`varying`, `ftransform()`).

## Approach: port UZDoom's GL backend, keep our scene layer

**Revised 2026-07-19** after measuring the actual coupling. Do **not** hand-modernize our 618 legacy
call sites, and do **not** try to take UZDoom's scene renderer. The split is clean:

| UZDoom component | Game-model coupling | Verdict |
|---|---|---|
| `src/common/rendering/gl/` (7.3k lines) | `AActor` 0, `sector_t` 0, `seg_t` 0, `FGameTexture` 0, `DVector` 0, `PClass`/`VMFunction` 0 | **PORT** |
| `src/common/rendering/hwrenderer/` (7.5k) | `AActor` 0, `sector_t` 0, `PClass` 0; `FGameTexture` in 3 files only | **PORT** (adapt 3 files) |
| `src/common/rendering/gl_load/` (4.6k) | none | **PORT** |
| `wadsrc/static/shaders/glsl/` (30 files) | assets | **PORT** |
| `src/rendering/hwrenderer/scene/` | `sector_t` 17 files, `AActor` 9, `FGameTexture` 6 | **KEEP OURS** — welded to the float sim / ZScript / modern texture model, which we must not adopt (netcode) |

So: **UZDoom's modern Core-profile backend + shaders come over wholesale; Zandronum's own scene layer
(which knows how to walk our fixed-point level data) is retargeted to emit into it.** This is both
less work than 618 hand-conversions and far better for pulling future UZDoom renderer features.

**Infrastructure adaptation surface** (what the ported backend needs that isn't the game model —
all ZDoom-lineage APIs Zandronum has in older form, so shim/rename work, not redesign):
`v_video.h` (DFrameBuffer/DCanvas integration — the main one), `filesystem.h` (UZDoom's FileSystem →
our `Wads`/FWadCollection, for shader lumps), `c_cvars.h`, `printf.h`, `matrix.h` (VSMatrix — pure
math, takes as-is), `m_png.h`, `i_time.h`, `r_videoscale.h`, `cmdlib.h`.

**What survives from Phase 0:** the 8 tested `computation/` units stay (pure, useful math). The
hand-rolled `gl_backend.cpp` / `renderer2d.cpp` / `irenderbackend.h` are **superseded** by UZDoom's
proven backend and get removed once the port lands.

## P1 status (2026-07-19)

**Compiling + linking into the engine (16 files):** `gl_load/gl_load.c`, `utility/matrix.cpp`,
`utility/stb_include.cpp`, `common/i_interface.cpp`, `common/zx_texbridge.cpp`,
`gl/gl_hwtexture.cpp`, `gl/gl_debug.cpp`, `gl/gl_samplers.cpp`, `gl/gl_buffers.cpp`,
`gl/gl_renderstate.cpp`, `gl/gl_shader.cpp`, `hwrenderer/data/hw_shaderpatcher.cpp`,
`hwrenderer/data/hw_bonebuffer.cpp`, `hwrenderer/data/hw_lightbuffer.cpp`,
`hwrenderer/data/hw_viewpointbuffer.cpp`.

**Adaptations landed:** device isolation (`zx_video.h` / `ZXFrameBuffer` / `ZXCanvas` / `zx_screen`
— renaming the *file* mattered too, since `-I src` first meant our `v_video.h` shadowed theirs);
C++20 for the vendored backend only (they need `std::strong_ordering`/`operator<=>`, engine stays
C++14); `TArray::Data()` + `TArray::begin()/end()` + `DVector2/3` + `TVector4` added to our headers;
`SHADER_*`/`TEXF_*`/`TM_*`/`CLAMP_*` enums and `DefaultRenderStyle()` in the bridge;
`palentry.h`/`renderstyle.h` shims; upstream `clamp()` and the 2D drawer dropped.

**The material/texture adapter is done** — `FMaterial::GetLayer()` adopts our `FGLTexture`'s GL
handle into an `OpenGLRenderer::FHardwareTexture`, so the vendored `ApplyMaterial` compiles
unmodified. `FTexture` grew the `FGameTexture`-shaped accessors the backend expects
(`GetTranslucency`, `GetShaderSpeed`, `GetGlossiness`, `GetSpecularLevel`, `GetTexture`,
`GetClampMode`, `isWarped`); the clamp-mode branching is a tested `ComputeClampMode` helper.
This adapter is tech debt — see issue #4.

**Bridges added this pass:** `common/filesystem.h` (`fileSystem` → our `Wads`, plus
`GetStringFromLump`), `common/printf.h` (a level-taking `DPrintf` overload; our console has no
per-level filtering so the level is discarded), `common/engineerrors.h`, `common/i_specialpaths.h`,
`common/zx_usershader.h`. `FString::Substitute` now returns whether it replaced anything, matching
UZDoom — existing callers ignore the result, so this is additive.

### Deliberately deferred (revisit before P4)

| Deferred | Why | Consequence |
|---|---|---|
| On-disk shader binary cache (`ZX_SHADER_BINARY_CACHE 0` in `gl_shader.cpp`) | needs UZDoom's `FileReader::OpenFile`/`ReadUInt32` and `FileWriter`, which our tree lacks | GLSL recompiles each launch; it is a startup-time optimization only |
| GLDEFS user shaders (`usershaders` is permanently empty) | we have no user-shader definition system | the shader manager's user-shader pass compiles nothing |
| `CheckNumForFullName`'s container argument | their second argument restricts the search to the core pk3 so mods cannot override core shaders; we have no equivalent | a mod-supplied shader with a core shader's name would win |
| `GetGlossiness`/`GetSpecularLevel` return 0 | no PBR material pipeline | PBR shaders render unlit |

**Next:** the backend is linked but still dormant — nothing drives it yet.

## P2 status (2026-07-19) — native SDL2 + a real Core context

**The macOS GL ceiling is gone.** The engine now links native SDL2 2.30.10 instead of the
sdl12-compat shim, so it can finally ask for a GL version and profile — something the SDL 1.2 API
simply cannot express, which is the whole reason macOS was stuck on a 2.1 compatibility context.

`vid_hwrender` (archived, restart-only) picks the profile, because a context is either core or
compatibility for its whole life:

| `vid_hwrender` | Context requested | State |
|---|---|---|
| `0` (default) | compatibility, 3.0 → 2.1 | legacy renderer, verified rendering correctly via MCP |
| `1` | core, 4.1 → 4.0 → 3.3 | context is created; nothing draws into it yet |

The fallback chain is a tested helper (`ComputeGLContextRequest`, 100%).

**Runtime-verified via MCP, not merely compiled:** with `vid_hwrender 0` the game renders MAP01
normally. With `vid_hwrender 1` every legacy GLSL lump fails to compile —
`version '120' is not supported`, `'varying' : syntax error`, `GL_EXT_gpu_shader4 is not supported`
— and the screen is black. That failure *is* the proof the core context is real: a compatibility
context would have accepted those shaders. It is also exactly what the coexistence constraint
predicted, and it is why P3 (retargeting the draw layer) has to come before core is usable.

**Also landed:** `sdl/sdlvideo.cpp` (the software framebuffer) and `sdl/SDLMain.m` are deleted —
the software renderer they served is gone, and SDL2 supplies its own main shim. Input moved to
SDL2's model: scancodes instead of keysyms (SDL2 keysyms for non-ASCII keys are `>= 1<<30` and
cannot index a table, whereas scancodes are dense and positional like the DIK codes they map to),
`SDL_TEXTINPUT` instead of the unicode field, `SDL_MOUSEWHEEL` instead of mouse buttons 4/5,
`SDL_WINDOWEVENT` instead of `SDL_ACTIVEEVENT`, and relative mouse mode instead of input grabs.
The table inversion is a tested helper (`ComputeInvertKeyTable`, 100%).

**Not verified:** actual keyboard/mouse input. The MCP injects events below the SDL layer, so it
cannot exercise the new scancode path; that needs a human at the keyboard.

**Still on the sdl12-compat build:** `build.sh` continues to build the 1.2 shim even though nothing
links it now. Removing it is Phase D cleanup.

## Phases

Every phase ends green (build + 90+ tests) and is **visually A/B'd via the MCP** (`launch_instance` →
`screenshot`) on a fixed scene set before the next begins.

> **SUPERSEDED — the phases below were written for hand-modernizing our own renderer.** The port
> approach above replaces them with: **P1** bring in UZDoom's `gl_load` + `common/rendering/gl` +
> `common/rendering/hwrenderer` + shaders, shimming the ~9 infrastructure APIs, compiling dormant;
> **P2** SDL2 migration for the Core context the ported backend requires; **P3** retarget our
> `gl/scene/*` draw code to emit into the ported `FRenderState`/buffers, subsystem by subsystem,
> MCP-verified; **P4** delete the legacy path (old `gl_renderstate`/`gl_shader`, immediate mode, the
> 52 `shadermodel` sites, `sdl12-compat`, and our bespoke `gl_backend`/`renderer2d`); **P5** Vulkan —
> now much cheaper, since UZDoom's `common/rendering/vulkan` sits behind the same ported abstraction.
> The subsystem ordering and legacy inventory below remain accurate and still drive P3.

### Phase A (historical) — Geometry to buffers, under the CURRENT 2.1 context
Migrate draw sites from immediate mode to VBO + `glVertexAttribPointer`, keeping GLSL 1.20 and the
compat context. Verifiable on macOS immediately. Legacy call counts in parentheses.

**A0 — prerequisites (must land first):**
- **CPU matrix stack** replacing the fixed-function one, using `matrix_compute`. `gl_scene.cpp`'s
  `SetProjection`/`SetViewMatrix` (:263/:298) is the root of every 3D matrix in the renderer —
  *nothing in `scene/` can move before this*. Includes replacing the `gl_TextureMatrix[7]`
  world-coord hack that `main.vp` reads with an explicit uniform.
- **`FFlatVertexBuffer::BindVBO`** (`gl_vertexbuffer.cpp:367`) from client arrays →
  `glVertexAttribPointer`. Single highest-leverage change in the tree.
- Route draws through `FRenderState` (`gl_renderstate.cpp`) — the existing "set state → Apply() →
  draw" seam — rather than adding a parallel path.

**A1 — ascending difficulty**, each A/B'd via MCP screenshot before the next:
decals (5) → weapon/psprites (12) → walls (`gl_walls_draw` 13 + `gl_vertex` 8, the 4 edge-split
helpers append into the wall's vertex run) → sprites (29 + `gl_spritelight` 3; `glColor4f` must become
a per-vertex attribute) → framebuffer state (7, mostly deletions) → **flats (20)** → models/voxels
(`gl_models` 37 — all matrix-stack; `gl_voxels`/md2/md3 11) → render-state + texenv
(`gl_interface.cpp`'s 39 `glTexEnvi` in `gl_SetTextureMode` → shader branches, **all 5 modes**; the
shader implements only 2 today) → render hacks/flood-fill (`gl_drawinfo` 26, stencil-ordering
sensitive) → 2D/HUD (`gl_renderer` 68; 8 primitive types, needs the batching 2D layer —
`renderer2d.cpp` + `quad_compute` already exist for this) → portals (58; stencil recursion +
`glClipPlane` → `gl_ClipDistance`) → **sky/skydome (150 — largest; pre-bake the dome into a static
indexed VBO, effectively a rewrite)** → **screen wipe (70 — hardest per line; `Wiper_Melt`'s
per-column quads and `Wiper_Burn`'s `glTexEnvi` multitexture combiners reuse nothing)**.

**On the artifact:** flats already draw via `glDrawArrays(GL_TRIANGLE_FAN)` from the VBO, and the
lighting attributes (`VATTR_LIGHTLEVEL`/`FOGPARAMS`/`GLOWDISTANCE`, `glColor4f`) are set **constant
per subsector**. The visible facets are therefore the **BSP subsector tessellation** showing because
adjacent subsectors of one sector resolve to slightly different constant lighting on arm64. Fixing
this means moving those attributes to real per-vertex data (A0 + flats), so verify the ceiling facets
specifically after the flats step.

### Phase B — SDL2 migration (only as the Core-context prerequisite)
Port `sdl/sdlglvideo.{h,cpp}`, `sdl/sdlvideo.{h,cpp}`, `sdl/hardware.cpp`, `sdl/i_input.cpp`,
`sdl/i_main.cpp` from the SDL 1.2 API to SDL2 (already bundled; drop `sdl12-compat` from `build.sh`).
**The reason we need this is `SDL_GL_SetAttribute(CONTEXT_MAJOR/MINOR/PROFILE_MASK)`** — SDL 1.2
cannot request a GL version/profile at all, which is why macOS is stuck at 2.1. Keep the port
minimal and behavior-preserving; incidental wins (in-place resize instead of the
destroy-and-recreate at `sdlglvideo.cpp:258-277`, `SDL_GL_SetSwapInterval` vsync on Linux,
`SDL_WINDOW_FULLSCREEN_DESKTOP`) come for free but are **not** goals here.
Also replace the fragile `strcmp`-on-version detection (`gl_interface.cpp:185-189`) with
`glGetIntegerv(GL_MAJOR_VERSION/GL_MINOR_VERSION)` — a real correctness prerequisite for Core.

### Phase C — Core profile: shaders, matrices, state
1. **Matrix stack → explicit uniforms** using `matrix_compute` (replace 32 `glMatrixMode` +
   push/translate/rotate/scale, and the `gl_TextureMatrix[7]` world-transform hack).
2. **Rewrite all 18 GLSL lumps** (`wadsrc/static/shaders/glsl/`) to `#version 330 core`:
   `attribute`→`in`, `varying`→`in/out`, `gl_FragColor`→`out vec4`, `texture2D`→`texture`,
   built-ins→`layout(location=)` inputs + explicit `uniform mat4`s, `gl_ClipVertex`→`gl_ClipDistance`.
   **Preserve the algorithms** in `main.fp` (`R_DoomLightingEquation`, `desaturate`, `getLightColor`,
   `getTexel`, `applyFog`, the DYNLIGHT loop).
3. **`gl_SetTextureMode`** (40 `glTexEnv`) → the shader `texturemode` uniform, covering **all five**
   modes (the shader currently implements only 2 of 5).
4. Alpha test → `discard`; `glClipPlane` → `gl_ClipDistance`; `GL_QUADS`/`GL_POLYGON` → triangles;
   `glColor4f` → per-vertex color attribute; add VAOs; `GL_GENERATE_MIPMAP` → `glGenerateMipmap`.
5. **Flip the context to Core** (SDL2 attributes + `WGL_CONTEXT_CORE_PROFILE_BIT_ARB` at
   `win32gliface.cpp:753`) once no fixed-function remains.

### Phase D — Delete the legacy path
Remove `FRenderState::Apply`'s fixed-function `else` branch and its `ff*` shadow state,
`gl_SetTextureMode`, the **52 `gl.shadermodel` conditional sites across 16 files** (hard-code
shadermodel 4), and the `sdl12-compat` dependency. One modern path, no dead code.

### Phase E — Vulkan/Metal (future)
A second `IRenderBackend` implementation behind the existing seam. No engine changes.

## Deliberately out of scope for this upgrade
Deferred at the user's direction — they are real bugs but not part of the GL upgrade, and chasing
them dilutes it:
- **Display gamma not restored on abrupt exit** (affects Windows *and* macOS). Root cause is known
  and recorded: restore only happens in the framebuffer destructor via `atexit`, so it never runs on
  signal death; `sdl/i_main.cpp:245` also wraps `cc_install_handlers` in `#if !defined(__APPLE__)`,
  so macOS has no crash handler at all. `gammaramp_compute` (built + tested) is ready when we return
  to it.
- Borderless / windowed-fullscreen option (tri-state video mode).
- Deleting the dead Windows D3D9/DDraw path (`fb_d3d9.cpp`, `fb_ddraw.cpp`), unreachable since
  `vid_renderer` is pinned to 1.

## Verification
- `cmake --build build-tests && ctest` + `bash tests/coverage.sh --auto` (100% on `computation/`).
- Engine: `cmake --build build-mac-arm`; then **`./build.sh`** to refresh `build/ZandroX.app` — the
  MCP launches that bundle, not `build-mac-arm` ([[mcp-launches-stale-app]]).
- Runtime: MCP `launch_instance` (windowed, MAP01) → `screenshot`, A/B each subsystem against the
  pre-migration image; `renderer_info` to confirm GL version/shadermodel.
- Netcode untouched by construction (draw-only); still smoke-test demo playback.

## Out of scope (permanently)
ZScript, floating-point sim, `FGameTexture`/`FMaterial`, GZDoom-4.x rebase, UZDoom gameplay features.

## P3 status (2026-07-19) — in progress

**Landed:** UZDoom's 30 GLSL lumps are installed at `shaders/hwrender/` (not `shaders/glsl/`, which
the legacy renderer still owns — both renderers ask for the same filenames, and only one copy of a
name can live in the pk3). The 53 path literals in the ported code were rewritten to match.
`hwrender_init.cpp` owns the core-profile flag and brings the ported shader manager up from
`OpenGLFrameBuffer::InitializeState()`, immediately after `gl_LoadExtensions()` — the earliest point
where the GL entry points exist.

**UZDoom's shader pipeline now compiles under a GL 4.1 core context on macOS** —
`hwrender: ported shader manager compiled in 51 steps`, with no errors attributable to the ported
set. Three distinct blockers had to be cleared to get there, each found by sampling the hung
process rather than by reading code:

| Symptom | Cause | Fix |
|---|---|---|
| Spin in `FShader::Load` | `zx_screen` was the P1 null stub; upstream only `assert`s `mLights`/`mBones` and asserts compile out in release | `ZXMinimalFrameBuffer` — a concrete `ZXFrameBuffer` implementing just `CreateDataBuffer` and the window-geometry virtuals, so their `OpenGLFrameBuffer` (and its renderer/postprocess cascade) stays out |
| Link errors on `ZXFrameBuffer` members | their bodies live in `v_video.cpp`, which we never ported | `common/zx_video.cpp` with minimal bodies for the handful the shader/buffer paths need |
| Spin in `FLightBuffer::FLightBuffer` | the ported backend resolves GL through **its own** loader, and `ogl_LoadFunctions()` is only called from the `gl_framebuffer.cpp` we do not compile — so every backend GL call went through an unloaded pointer | call `ogl_LoadFunctions()` first in `InitPortedShaders()` |

**Still black under `vid_hwrender 1`.** The legacy renderer continues to initialize and still owns
every draw call; its 18 `#version 120` lumps produce ~960 compile errors in a core context, exactly
as expected. Nothing is drawn through the ported path yet.

**Next:** stop the legacy renderer from initialising under core, then bring up the ported
`FGLRenderer` / 2D drawer so there is something to draw with.

**Superseded blocker (kept for history).** `FShader::Load` dereferences `zx_screen->mLights` and
`zx_screen->mBones` unconditionally; upstream only `assert`s them, and asserts compile out in
release. Our `zx_screen` is the null stub added in P1 to satisfy the linker, so the first shader
compile walked into a null dereference and the crash handler spun — the process pegged a core and
never came up. `InitPortedShaders()` now refuses with a message unless `zx_screen` and both buffers
are live, so `vid_hwrender 1` degrades to "no shaders" instead of hanging.

**Next:** stand up a real `ZXFrameBuffer` with `FLightBuffer` and `BoneBuffer` (both already
compiling since P1) and a working `CreateDataBuffer`, assign it to `zx_screen`, then let the shader
manager compile. Only after that can anything be drawn through the ported path.

### Build trap worth fixing (relates to task #27)

`add_pk3` runs `zipdir -udf` with `DEPENDS zipdir` only — it does not depend on the contents of
`wadsrc/static`. Adding the 30 shader lumps did **not** rebuild `zandronum.pk3`; it kept reporting
"contains 715 files (updated 0)" until the pk3 was deleted by hand. Any future shader edit will be
silently ignored the same way.

## P3 status (2026-07-19, end of session)

Under `vid_hwrender 1` MAP01 renders through the ported core path: walls, flats, sprites, sector
lighting, the player weapon and the HUD. `vid_hwrender 0` is untouched throughout.

| Subsystem | Route | State |
|---|---|---|
| 2D / HUD | `DrawTextureV` → queue → `Renderer2D` | works; clipping, color overlay, render styles ignored |
| Walls | `GLWall::RenderWall` → scene queue | works; split edges, glow, per-side color dropped |
| Flats | `GLFlat::DrawSubsector` → scene fan | works; VBO fast path bypassed under core |
| Sprites | `GLSprite::Draw` → scene fan, translucent | works; untextured particles skipped |
| Weapon | `DrawPSprite` → 2D queue with explicit UVs | works |
| Lighting | `gl_SetColor` captured instead of `glColor4f` | works; both lightmode branches |
| Fog | — | **not ported** |
| Dynamic lights | — | **not ported** |

View and projection are rebuilt by `ComputeSceneProjection`/`ComputeSceneView` (8 tests), since core
removes `glMatrixMode`/`gluPerspective`. The camera is captured in `SetViewMatrix`, not `SetupView` —
the latter is portal/skydome only, and hooking it left the scene camera unset.

### The next step is verification, not more subsystems

Everything above was checked by eye, on one scene, in one map. That is not sufficient, and this
session gave three concrete reasons to distrust it:

- the build check grepped only `error:`, so `make: *** [all] Error 2` read as success for several
  cycles and every test in that window ran a stale binary;
- the `add_pk3` fix was "verified" against `zandronum.pk3` while it broke `brightmaps.pk3`;
- two hangs were misdiagnosed from reading code, and only `sample` on the live process found them.

So before fog and dynamic lights — which interact with the lighting just ported and whose errors
look entirely plausible — build an A/B harness: same map and viewpoint, both paths, images compared
automatically. That also settles the question this work started from, whether the arm64 triangular
faceting survives the upgrade. Current impression is that it does not appear under core, from a
single MAP01 screenshot; that is an observation, not a result.

## P4 — not started

Blocked on P3 reaching verified parity. Scope: the fixed-function branch in `FRenderState::Apply`
and its `ff*` shadow state, `gl_SetTextureMode`, the 52 `gl.shadermodel` sites across 16 files, the
18 legacy `#version 120` lumps at `shaders/glsl/`, the bespoke `gl_backend.cpp`/`renderer2d.cpp`
scaffolding once UZDoom's own renderer replaces it, and the sdl12-compat build step, which nothing
has linked since P2.

## Known core-path defects (user-reported, 2026-07-19)

Observed by eye on the running core path, and not yet diagnosed. The A/B metric currently sits at
25.57% differing / MAE 3.88 on MAP01; these are almost certainly part of that residual.

| Defect | Leading hypothesis | Where to look |
|---|---|---|
| In-engine font text renders inverted | The 2D path ignores `DrawParms` render style, color overlay and translation. Fonts draw with a translation and often a non-Normal style, so a paletted/shaded font texture is being sampled as if it were plain RGBA. | `OpenGLFrameBuffer::DrawTextureV` hook — it currently passes a flat white tint and drops every field of `parms` except position, size and alpha |
| Enemies appear half cut off | **Queue ordering ruled out.** Splitting the replay into opaque-then-translucent passes changed the metric by 0.016pp and did not fix it, so it is not later opaque geometry erasing depth. Next hypothesis: sprite billboards intersect the floor/stair flats they stand on, and the legacy path applies clipping or a depth offset there that we do not. | `GLSprite::Draw` z1/z2 handling and whatever floor-clip the legacy path applies before emitting |
| Sprite lighting looks odd | `SetSurfaceColor` is captured globally at `gl_SetColor` time and read at queue time. If any draw is queued without its own `gl_SetColor` first, it inherits the previous surface's color. | `gl_SetColor` hook in `gl_lightdata.cpp` and the `s_surf*` globals |

The font one is the most clearly wrong and the cheapest to confirm: `DrawTextureV` already parses a
full `DrawParms` and we throw nearly all of it away.

### Limitation of the A/B metric

The pass-split experiment moved the number from 25.5706% to 25.5542% — 0.016pp — while the sprite
defect it targeted is plainly visible in the capture. The metric is area-weighted, so it is dominated
by walls, flats and lighting; a defect confined to a few sprites barely registers.

So the number is the right tool for *lighting and shading* regressions and useless for small-area
defects. Both are needed: the metric to catch broad shifts, and looking at the image to catch things
like inverted font glyphs and clipped sprites. Neither substitutes for the other, and a falling
metric must not be read as "the remaining defects are small".

### Do not hand-fix the current defects — compared against UZDoom

Checked whether the planned work subsumes the reported defects, rather than patching them in the
bespoke path:

| Defect | UZDoom's equivalent | Verdict |
|---|---|---|
| Enemies half cut off | `HWSprite::PerformSpriteClipAdjustment` in `rendering/hwrenderer/scene/hw_sprites.cpp:698`, with the `gl_spriteclip` cvar and `floorz` handling | fixed by adopting their sprite path |
| Inverted font text | `common/2d/v_2ddrawer.cpp` (1259 lines) handles `colorOverlay`, `mTranslationId` and render styles | fixed by adopting their `F2DDrawer` |
| Odd sprite lighting | same sprite path, which carries its own light/color rather than a captured global | fixed by the same adoption |

So `Renderer2D` and the queue in `hwrender_init.cpp` are **scaffolding**, not the destination. They
proved the core context, shaders, buffers and draw path work end to end, which was their job.
Re-implementing sprite floor-clip and the full `DrawParms` semantics inside them would be reinventing
code we already have vendored, in a file P4 deletes.

**Revised order.** Bring up UZDoom's `FGLRenderer`, `F2DDrawer` and `HWSprite` against the working
core context, move the engine's draw calls onto those, and retire the bespoke seam — instead of
polishing the seam first. The A/B harness measures that migration the whole way.

## Adopting UZDoom's renderer — first measurements (2026-07-19)

Direction set by the user: stop supporting both paths and drop what is going away, rather than
polishing the bespoke seam.

`common/2d/v_2ddrawer.{h,cpp}` is now vendored (1259 + 384 lines). Its dependency cost is small:

- Missing headers: `vm.h`, `v_draw.h`, `fcolormap.h`, `texturemanager.h` — four.
- **ZScript coupling is shallow.** 41 VM references, but they sit in 10
  `DEFINE_ACTION_FUNCTION_NATIVE` blocks totalling 70 of 1259 lines (5.6%), all wrapping
  `DShape2DTransform` for script. Dropping those blocks and the `vm.h` include leaves ~94% of the
  drawer intact — including the `colorOverlay`, `mTranslationId` and render-style handling whose
  absence is causing the inverted font glyphs.

So `F2DDrawer` is adoptable without dragging in ZScript, which was the open question. `hw_draw2d.cpp`
(255 lines, already vendored) consumes it and emits through the ported `FRenderState`, which compiles.

**Next:** strip the script-export blocks from `v_2ddrawer.cpp`, shim the remaining three headers the
way `filesystem.h`/`printf.h` were shimmed, compile it with `hw_draw2d.cpp`, and route the engine's
2D through it — retiring `Renderer2D`'s 2D path. Then the same exercise for `HWSprite` (which brings
`PerformSpriteClipAdjustment`) and `FGLRenderer`.

## F2DDrawer switchover — attempted and backed out (2026-07-19)

Routing `DrawTextureV` into `F2DDrawer::AddTexture` and emitting via their `Draw2D` **works
mechanically but stalls the frame**, badly enough that the main loop stops reaching
`MCP_Bridge_Poll()` and the engine services no console input at all. Backed out; the 2D queue is
live again and the build is usable. Everything else from the attempt is kept.

Four null dereferences had to be fixed to get that far, each found by sampling the hung process:

| Null | Why it mattered |
|---|---|
| `CreateVertexBuffer`/`CreateIndexBuffer` returning nullptr on `ZXMinimalFrameBuffer` | their 2D drawer builds its vertex buffer at startup without checking |
| `zx_screen->mViewpoints` | `Draw2D` calls `Set2D` on it unconditionally |
| layer 0 in `ApplyMaterial` | ours comes from the FTexture→FMaterial adapter and can legitimately fail |
| `FTexture::CreateTexBuffer` | see below |

**The most valuable finding: `CreateTexBuffer` is now real.** The P1 stub assumed
`FMaterial::GetLayer`'s adopted GL id would always short-circuit the uploader's create-branch. That
holds for the scene path but **not** for 2D — `Bind()` misses and the create-branch runs, so it got
an empty buffer. It now produces true-color pixels via our own `CopyTrueColorPixels`, bridging to
their uploader without porting `FGameTexture` (issue #4).

**The open question is why `Bind()` keeps missing.** `FMaterial::ValidateTexture` caches on
`tex->gl_info.Material`, so the material and its `mAdoptedLayers` persist across frames, and the
adopted handle should make `Bind()` hit on frame two. It evidently does not, so every draw re-runs
`CreateTexBuffer` — a full CPU-side texture render per quad per frame, which is the stall. Start
there: instrument `FHardwareTexture::Bind` under core and find why the cached handle is not taken.

Once that is understood the switchover should be a small change, since everything else it needs is
already built and linked.

## F2DDrawer switchover — second attempt (2026-07-19)

**The stall is solved.** It was never caching or `CreateTexBuffer`; it was two more null globals that
the crash handler turned into uninterruptible spins:

| Null | Fix |
|---|---|
| `GLRenderer` (the P1 stub) — the texture uploader dereferences `GLRenderer->mSamplerManager` on *every* bind | construct a real `FGLRenderer` with an `FSamplerManager`, plus a minimal ctor/dtor since their `gl_renderer.cpp` is not compiled |
| `zx_screen->mVertexData` — `Draw2D` restores this vertex buffer at the end of every frame | compile `flatvertices.cpp` and construct an `FFlatVertexBuffer` |

The second required renaming our legacy `FFlatVertexBuffer` to `LegacyFlatVertexBuffer` (4 files,
21 refs), the same yielding-to-UZDoom's-names move the `TM_*` rename made.

**With those fixed the switchover runs — the main loop stays responsive — but draws no 2D at all**,
and the A/B went 25.55% → 69.19% (scene visibly darker, HUD absent). Routed back to the queue.

The cause is almost certainly the null-layer guard added to `ApplyMaterial`: it turns a failed
`FMaterial::GetLayer` into a silent skip, so if the lookup fails for 2D textures *every* quad is
dropped. That guard stopped a crash and hid a bug, which is worth remembering as a pattern.

**Next:** instrument `FMaterial::GetLayer` under the 2D path and find why it returns null there when
it succeeds for scene geometry. The whole pipeline is built, linked and running; this is the last
thing between it and working 2D.

### Null-global spins are now a tested computation

Three separate hangs this session were the same bug: the ported backend reaches through a global our
tree leaves null, and a null does not fault cleanly -- the crash handler spins, so it is only
findable by sampling a hung process. `computation/backendprereq_compute.{h,cpp}` turns that into a
named failure before anything draws, covering `zx_screen`, its light/bone/viewpoint/vertex buffers,
`GLRenderer` and its sampler and shader managers, reported in the order the backend touches them.
Four tests, including one asserting the *first* missing item is named so the message points at the
root cause rather than a downstream symptom.

## F2DDrawer — narrowed to the render state (2026-07-19)

Two hypotheses tested and **both wrong**, which is worth recording so they are not retried:

1. *"`FMaterial::GetLayer` fails for 2D textures, so `ApplyMaterial`'s null-layer guard drops every
   quad."* Instrumented both null exits in `GetLayer`; **zero** failures across a full run. The
   material lookup works fine for 2D.
2. *"The drawer's size is never set, so the 2D projection is degenerate."* Setting it changed
   nothing.

Instrumenting the drawer itself settles where the problem is **not**: at `Draw2D` time it holds
**54 commands, 244 vertices, size 800x600** — correct input, correct size. So `DrawTextureV` →
`AddTexture` works, the material path works, and sizing works.

**The gap is downstream, in how the ported `FGLRenderState` applies shaders and state.** The
symptoms fit: nothing 2D appears *and* the scene renders darker, i.e. state the 2D pass sets is both
failing to produce output and leaking into the next frame's scene pass. The ported shader set
compiles (51 shaders) but has never actually drawn anything — every pixel on screen so far comes
from the bespoke `Renderer2D`. That makes `FGLRenderState::ApplyState`/`Apply` the place to look,
not the drawer.

Routed back to the queue; measures 25.55%.

### Root cause located: the ported render state raises GL errors

`glGetError()` around `Draw2D` reports **`GL_INVALID_ENUM` (0x500) on the first frame and
`GL_INVALID_OPERATION` (0x502) thereafter**. So the draws are being rejected by the driver — that is
why nothing appears, and why state leaks into the next frame's scene pass.

Ruled out along the way: `F2DDrawer` input (54 commands, 244 vertices, correct size),
`FMaterial::GetLayer` (zero failures), drawer sizing, and the obvious Core-removed state —
`ApplyState` uses only clip distances and polygon offset, and there is no `GL_ALPHA_TEST`,
`glTexEnv` or `GL_QUADS` anywhere in the ported render state. `GLVertexBuffer::Bind` uses plain
`glVertexAttribPointer`, which is Core-legal, and a VAO is bound by the bespoke backend.

**Use `FGLDebug` rather than guessing.** `gl/gl_debug.cpp` has been compiled since P1 and installs a
`glDebugMessageCallback`; wiring it up under core will name the exact offending call instead of
bisecting by hypothesis. Three wrong guesses were made here (caching, `GetLayer`, drawer size) before
reaching for `glGetError` — the debug callback should have been first.

Likeliest remaining suspects, in order: the shader program bind (the ported shader set compiles but
**has never drawn a pixel**, so `FShader::Bind` is entirely unexercised), and the uniform-buffer
binding points for the viewpoint and light buffers.

**The debug callback is unavailable on macOS.** `FGLDebug` is now installed under core, but
`KHR_debug` is a GL 4.3 feature and Apple caps at 4.1, so `HasDebugApi()` is false and it emits
nothing. It will still help on Windows and Linux once those are built.

So the failing call has to be found by **bracketing `glGetError()` through `Draw2D`** — around the
viewpoint `Set2D`, the state block, `SetVertexBuffer`, the material apply, and the draw itself — and
bisecting. That is mechanical and bounded; each bracket is one build-and-run.

### Bisected to two functions

Bracketing `glGetError()` down through `Draw2D` → `DrawIndexed` → `Apply` isolates it exactly:

| Site | Error | Count per frame |
|---|---|---|
| `FGLRenderState::ApplyState` | `GL_INVALID_ENUM` (0x500) | 26 |
| `FGLRenderState::ApplyShader` | `GL_INVALID_OPERATION` (0x502) | 4 |

`glDrawElements` itself is clean — `dt2gl` maps correctly and `GL_UNSIGNED_INT` is valid. The draw
is rejected because the state set immediately before it is invalid.

`ApplyState` does four things: `ApplyBlendMode`, the `GL_CLIP_DISTANCE3/4` toggles, `ApplyMaterial`,
and polygon offset. Clip distances are GL 3.0 and fine, so the invalid enum is most likely inside
`ApplyMaterial` — the sampler manager's `glSamplerParameteri`, or a texture parameter such as an
anisotropy or LOD-bias enum that Apple's 4.1 does not accept. Bracket those four in turn.

`ApplyShader` failing with an invalid *operation* fits a uniform or uniform-block call against a
program that is not current or whose block index is unset — the ported shader set compiles but has
never been bound for a real draw, so `FShader::Bind` and the viewpoint/light UBO binding points are
completely unexercised.

Both are now single-function problems rather than "the 2D does not draw", which is where this
started.

### One root cause fixed: `GL_GENERATE_MIPMAP`

Bracketing further put the `GL_INVALID_ENUM` inside `ApplyMaterial` → `FMaterial::GetLayer`, i.e.
**our own adapter**, which routes through the legacy texture upload path. That path sets

    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, ...)

which was deprecated in GL 3.0 and **removed in core** — an `INVALID_ENUM` on every texture bind, 26
per frame. Phase C of this plan already listed that exact migration; it simply had not been done yet.
Gated off under core, and the errors are gone.

Core textures are now unmipmapped: the replacement is `glGenerateMipmap`, but our legacy GLEW loader
does not resolve it (`__glewGenerateMipmap` undefined at link). The ported backend has its own loader
that does, so routing the upload through `FHardwareTexture::CreateTexture` will fix this properly.
Costs quality at distance, not correctness.

**Still blocking: `ApplyShader` raises `GL_INVALID_OPERATION`.** With the enum fixed the 2D still does
not draw and the A/B stays at 69%, so this is the remaining cause. Bracket inside `ApplyShader` next:
the ported shader set compiles but has never been bound for a real draw, so `FShader::Bind`, the
uniform uploads and the viewpoint/light UBO binding points are all unexercised. An invalid *operation*
there most likely means a uniform or uniform-block call against a program that is not current, or a
block index that was never resolved.

### Second root cause — `glUseProgram` theory DISPROVEN

Bracketing through `ApplyShader` lands on `FShader::Bind` → `FShaderManager::SetActiveShader` →

    glUseProgram(sh->GetHandle());

28 `GL_INVALID_OPERATION` per frame, and `GL_CURRENT_PROGRAM` afterwards reads **1** — a suspiciously
low id that is almost certainly a legacy program, i.e. the call failed and left the previous binding.

`glUseProgram` raises invalid-operation when the handle is not a valid **linked** program. So despite
`InitPortedShaders` reporting "compiled in 51 steps", the programs it produced are not usable. The
compile loop pushes whatever `Compile()` returns without checking link status, so a failed link is
recorded as success.

**Next:** in `InitPortedShaders`, after the compile loop, query `glGetProgramiv(handle,
GL_LINK_STATUS)` for each shader and log the failures with `glGetProgramInfoLog`. That will say
exactly why they do not link — most likely the `#version 330 core` lumps hitting something Apple's
GLSL rejects, or a missing attribute/uniform binding. The shader *compile* being reported as fine
while the *link* silently failed is the same shape of bug as the earlier stubs: a success path that
never verified anything.

**Correction: the shaders link fine.** Verifying `GL_LINK_STATUS` after the compile loop reports
**30/30 ported programs linked** (both alpha-test variants, normal pass). So `glUseProgram` is not
being handed an invalid program and the previous conclusion was wrong.

That leaves `ApplyBuffers`, which sits between `ApplyState` and `ApplyShader` in `Apply()` and was
**never bracketed** — the `AS:Bind` probe was the first inside `ApplyShader`, so it also caught
anything `ApplyBuffers` left behind. `ApplyBuffers` binds the vertex and index buffers and the
viewpoint/light UBOs via `glBindBufferRange`, where a misaligned offset or a buffer that was never
sized raises exactly `GL_INVALID_OPERATION`. Our `ZXMinimalFrameBuffer` constructs those buffers with
placeholder sizes, which makes it a strong candidate.

**Next:** bracket `ApplyBuffers` directly, and check the `uniformblockalignment` the ported code uses
against what the driver reports for `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT`.

The link-status check is worth keeping regardless — it converts "compiled in N steps", which only
counted iterations, into a real assertion about outcomes.

### The remaining GL error may be ours, not UZDoom's

Bracketing `ApplyMaterial` shows the `GL_INVALID_OPERATION` arriving at its **entry** — before
`GetLayer`, before `BindOrCreate`. Combined with `ApplyBuffers` measuring clean and all 30 shader
programs linking, nothing inside UZDoom's `Apply()` has been shown to raise it.

What has not been checked: **our own bespoke scene pass runs earlier in the same frame**, through
`GLBackend` and `Renderer2D`, and `RenderCoreFrame` calls it before `Draw2D`. An error it leaves
behind would surface at the first probe inside the ported code and look like UZDoom's fault. Three
successive "it's in X" conclusions have now been wrong the same way — attributing an error to the
first call after a probe rather than to the unmeasured gap before it.

**Next:** clear `glGetError()` immediately before `Draw2D` and re-measure. If the errors vanish they
are ours and the ported path may be clean; if they persist, bracket `ApplyBlendMode` and the clip
distance toggles, which are the only calls in `ApplyState` ahead of `ApplyMaterial` still unmeasured.

### Correction: the GL errors were a red herring

Draining `glGetError()` immediately before `Draw2D` and counting again after gives:

    drained=1 before Draw2D      (ours, from the bespoke scene pass)
    errors=1  after Draw2D       (UZDoom's, per frame)

**One error per frame, not 23-28.** The earlier counts were the same single error observed repeatedly
at probe points that were not draining consistently — the probe was measuring itself.

And one `GL_INVALID_OPERATION` per frame cannot blank 54 draw commands. So the GL errors are a
separate, minor issue and **not** why the 2D is invisible. Several turns of bisection were spent on
the wrong symptom.

What is actually established about the 2D failure:

- the drawer receives correct input — 54 commands, 244 vertices, 800x600
- `FMaterial::GetLayer` succeeds for 2D textures (instrumented, zero failures)
- all 30 ported shader programs link
- `ApplyBuffers` is clean
- exactly one GL error per frame, far too few to drop every quad

None of that explains invisible output, which points at something that fails *silently*: geometry
landing outside the viewport or clip volume, a projection from `mViewpoints->Set2D` that does not
match the screen, blending or depth state discarding every fragment, or writes going to a target that
is never presented. **Next: read back a pixel with `glReadPixels` right after `Draw2D` to establish
whether anything is reaching the framebuffer at all** — that separates "not drawn" from "drawn and
then lost", which is the split none of this bisection has actually tested.

### Confirmed by eye (user, 2026-07-19) — two defects still present on the queue path

Re-checking the captures against compat, both of these are **still broken** and earlier assessments
that they looked fine were wrong:

| Defect | Detail |
|---|---|
| HUD partially drawn | The numbers (`100/100`, `50/200`) render; the **medkit and ammo icons do not**. Compat draws both. So 2D is not uniformly broken — font characters appear and patch graphics do not. |
| Sprites cut in half | Both zombiemen on the stairs are truncated at the legs. The wall-pass fix moved the metric 0.004pp, which should have been read as "changed nothing" rather than as a fix. |

The HUD split is a **new and useful clue**: font chars and patches are different texture classes, so
this looks like the material/handle lookup failing for one class rather than the 2D path being
broken as a whole. That is testable directly — log `GLHandleFor` per `UseType` and see which class
returns 0.

Note on method: four separate visual assessments in this session were wrong where the user's reading
was right. The area-weighted metric is nearly blind to these — icons and sprite legs are a tiny
fraction of pixels — so screenshots must be read against compat deliberately, element by element,
not glanced at.

### Handle-lookup probe — BOTH readings were wrong; see the correction below

Logging `GLHandleFor` per `UseType` reported zero failures across usetypes 1 (Wall), 2 (Flat),
3 (Sprite) and 9 (FontChar) — with `TEX_MiscPatch` (8), which is what the HUD icons are, absent
entirely.

**That does not mean MiscPatch fails.** The counts sum to exactly 400, the cap the probe was given,
so it exhausted itself on scene textures before the HUD ever drew. It answers nothing about the
icons. Recording it because the shape is seductive — an absent row reads like a finding when it is
just a truncated sample.

Redo it with the cap removed, or counting only after the scene pass, and it will actually answer
whether the icons reach the queue at all. If `TEX_MiscPatch` never appears even then, the icons are
drawn through a path that is not `DrawTextureV` and the hook is in the wrong place — which would be a
much better explanation for "numbers draw, icons do not" than any texture failure.

**Redone uncapped, per frame: `TEX_MiscPatch` (8) is genuinely absent.** A full frame reports only
usetype 1 (Wall, 43), 2 (Flat, 66), 3 (Sprite, 12) and 9 (FontChar, 59), with zero lookup failures
anywhere. The HUD icons never reach `GLHandleFor`.

So the user's observation — numbers draw, icons do not — is **not a texture problem at all**. There
is a 2D draw path that `DrawTextureV` does not cover, and the icons go through it. `DCanvas` has
several other virtuals we never hooked: `DrawBlock`, `DrawLine`, `DrawPixel`, `DrawBlendingRect`,
plus `Dim`, `Clear`, `FlatFill` and `FillSimplePoly` on `OpenGLFrameBuffer`, none of which are routed
under core.

That also reframes the wider 2D gap: the queue only ever sees what `DrawTextureV` hands it, so any
console background, dimming, or status-bar element drawn another way is silently missing. Worth
checking against the compat capture element by element rather than assuming `DrawTextureV` is the
whole 2D surface.

**Next:** find which call actually draws the status-bar icons — likely `DrawTexture` through a path
that bypasses `DrawTextureV`, or one of `Dim`/`FlatFill` — and hook it. That is a much cheaper fix
than the `F2DDrawer` switchover, and it is independent of it.

**Narrowing the unhooked path.** `DBaseStatusBar::DrawImage` goes through
`screen->DrawTexture(...)`, which funnels into `DrawTextureV` — so the classic status bar *would*
hit our hook. The capture shows the **fullscreen/alt HUD** (numbers in the corners, no status bar),
which is drawn elsewhere, so that is where to look: `DrawFullScreenStuff` / the alt-HUD path in
`shared_sbar.cpp` and `st_hud.cpp`.

The sharper next query is to instrument `DrawTextureV` itself rather than `GLHandleFor`, logging the
`UseType` of everything it receives. That distinguishes two very different situations we currently
cannot tell apart:

- the icons never reach `DrawTextureV` → they are drawn by a path we have not hooked, and the fix is
  to hook it;
- they reach `DrawTextureV` but never reach `GLHandleFor` → something between the two drops them, and
  the fix is in our own queueing.

Everything so far has measured the second half of that chain only.

### Correction (user, direct observation): the icons draw, they are **cropped**

The user can see the running game: the HUD icons **are** being drawn, they are cropped out of the
visible area. That supersedes the "unhooked draw path" conclusion, which was inferred from
`TEX_MiscPatch` being absent in a probe — a third wrong reading of that same measurement.

If they draw but are clipped, the pipeline is fine and the **2D viewport or projection is wrong**.
The strong candidate is `GetHeight()` vs `GetTrueHeight()`: `Renderer2D::Begin` sets
`SetViewport(0, 0, w, h)` and an ortho of the same extent, using the value `RenderCoreFrame` is
handed, while the engine positions the HUD relative to the *true* height. Drawing into the shorter
rect crops exactly the bottom edge, which is where the icons sit.

`OpenGLFrameBuffer::GetScreenshotBuffer` already reads back with
`(GetTrueHeight() - GetHeight()) / 2` as its Y origin, so the engine plainly expects a letterboxed
framebuffer taller than the visible screen. The core path ignores that distinction everywhere.

**Next:** pass `GetTrueHeight()` into `RenderCoreFrame` and offset the 2D ortho by
`(GetTrueHeight() - GetHeight()) / 2`, matching what the screenshot path already does. Then re-check
the icons against compat.

Method note: three separate conclusions were drawn from the usetype probe and all three were wrong.
A user looking at the screen resolved it in one sentence. When someone can observe the artefact
directly, ask them what they see before building inference on a proxy measurement.

**The letterbox hypothesis is disproven.** `GetTrueHeight()` returns `GetHeight()` on SDL — both
`v_video.h:448` and `sdlglvideo.h:66` — so there is no letterboxing on this platform and the proposed
ortho offset would have been a no-op. Checked before implementing, unlike several earlier guesses.

So the crop has another cause. What the core 2D path ignores from `DrawParms`, any of which could
clip or mis-place a quad:

- `windowleft` / `windowright` — the horizontal source window
- `dclip` / `uclip` / `lclip` / `rclip` — the clip rectangle
- `flipX`, `keepratio`, `bilinear`, `masked`
- `style` / `alphaChannel` / `specialcolormap` / `colormapstyle`

`Queue2DTexture` takes only `x`, `y`, `destwidth`, `destheight` and alpha. If the status-bar elements
rely on any of the above — particularly the clip rectangle or the source window — they would draw at
the wrong place or size, which is what "drawn but cropped" looks like.

**Next:** log the full `DrawParms` for the HUD draws specifically (filter on a small `destheight`, or
on `UseType`), and compare the values against what the quad ends up as. That is a direct comparison
rather than another inference, and it covers the fields the queue currently discards.

### HUD crop SOLVED: the texture offsets were being ignored

Logging `DrawParms` for small draws made it immediate:

    pos=19.0,596.0  dest=28.0x19.0  ut=3   <- health icon (TEX_Sprite)
    pos=43.0,583.0  dest=8.0x8.0    ut=9   <- font characters

At 800x600 the icon is placed at y=596 with height 19, so it runs to y=615 — fifteen pixels below the
screen. `DrawParms` carries `left` and `top`, the texture's own offsets, which must be **subtracted**
from the position. Sprites have them; font characters do not. That is exactly why text rendered
correctly while the sprite-based icons fell off the bottom, and why `TEX_MiscPatch` never appeared —
the icons are `TEX_Sprite`, which the probe *did* see and which I misread as scene sprites.

`Queue2DTexture` now uses `parms.x - parms.left` and `parms.y - parms.top`. A/B moved 25.3602% →
25.3373%, MAE 2.717 → 2.655.

**How this was actually found:** the user said "the icons are drawing, they're just being cropped".
Three conclusions drawn from the usetype probe were wrong, and the letterbox hypothesis that replaced
them was disproven before implementation. What worked was printing the actual values and reading
them. One line of real data beat five rounds of inference.

### Clipped sprites: it is not missing geometry adjustment

Correcting an earlier assumption. Our engine **already has** sprite floor-clip adjustment —
`gl_sprite.cpp:678-740`, gated on the `gl_spriteclip` cvar, handling corpses, tall graphics and
ceiling intersection. It runs in `Process()`, before the draw, so the `z1`/`z2` (and therefore the
`v1..v4`) the core hook receives are **already adjusted**. Adopting UZDoom's
`PerformSpriteClipAdjustment` would be duplicating logic we have.

So the vertices are right and the sprite is still cut, which points at **depth state**, not geometry:

- sprites are queued translucent, so they test depth but do not write it;
- opaque geometry is replayed first, so the floor and stairs are already in the depth buffer;
- a Doom sprite billboard *intersects* the floor it stands on, so its lower portion is genuinely
  behind that geometry and gets depth-rejected.

The legacy path avoids this — the same scene renders correctly in compat — so it must apply a depth
bias, a depth-range tweak, or draw sprites with different depth state. **Next: find what the legacy
renderer does about sprite/floor depth intersection** (look for `glPolygonOffset` or `glDepthRange`
around the sprite pass) and mirror it, rather than porting `HWSprite` for logic we already have.

**The legacy path varies depth state per pass; the core path does not.** Grepping shows
`glDepthFunc` switching between `GL_LESS`, `GL_LEQUAL` and `GL_EQUAL` and `glPolygonOffset` being
applied and reset across the draw lists — `gl_scene.cpp:494-579`, `gl_walls_draw.cpp:361-423`,
`gl_portal.cpp` throughout. Our core hook collapses all of it into a single
`SetDepthTest(true, !translucent)` at the default `GL_LESS`, with no polygon offset at all.

That is very likely the sprite cut: the legacy renderer gives the sprite pass depth state that lets a
billboard survive intersecting the floor it stands on, and we do not reproduce it. It would also
explain other subtle differences in the remaining 25%.

**Next:** capture which depth func and polygon offset are in effect at each `RenderWall`/`GLSprite`
call site, and carry them into the queue per draw instead of assuming one global setting. The seam
already has `SetDepthTest`; it needs a depth-func and depth-bias parameter alongside.

**Depth bias tried and rejected.** Applying the legacy decal offset (`-1.0`, `-128.0`) to translucent
draws did not fix the truncated sprites: A/B went 25.3373% → 25.3627% and MAE 2.655 → 2.658, i.e.
marginally worse, with the sprites still visibly cut. Reverted; `IRenderBackend::SetDepthBias` is
kept because the legacy path varies polygon offset per pass and the seam will need it.

So the cut is not a simple depth-bias problem either. Remaining candidates, none tested:

- the sprite is drawn in the wrong **order** relative to the flats it stands on, so the depth already
  written is not what the legacy path would have written at that point;
- the legacy sprite pass runs with a different `glDepthFunc` (`LEQUAL` vs `LESS`), which is a
  one-line experiment;
- the truncation is not depth at all but geometry — the `z1`/`z2` reaching the hook may not be the
  adjusted values, which can be settled by printing them next to `thing->floorz` for a zombieman,
  exactly as printing `DrawParms` settled the HUD crop in one run.

The last of those is the cheapest and most direct, and printing real values has resolved every
question this session that inference did not.

**Fixed and confirmed on screen:** the HUD icons now render — medkit bottom-left, ammo bottom-right.

### Sprite truncation: the sprites are innocent

Printing the sprite quad against the actor's floor settles it:

    z1=111.0  z2=56.0  floorz=56.0
    z1=112.0  z2=56.0  floorz=56.0
    z1=93.0   z2=40.0  floorz=40.0

`z2` — the quad's bottom — equals `floorz` **exactly**, every time. The geometry reaching the hook is
correct and already clip-adjusted, so the sprite path is not at fault and neither the depth bias nor
`HWSprite` adoption would have helped. Task #33's premise was wrong twice over.

The sprites in the capture stand on **stairs**, so whatever truncates them is drawn *in front of*
them: our flats. If step surfaces are emitted slightly too high, or the fan triangulation of a
subsector puts a step's leading edge where it should not be, the legs get covered — and the same
error would be invisible on open floor, which is why nothing else looked wrong.

**Next: compare a flat's emitted vertex heights against `plane.ZatPoint` for a stair subsector**, the
same print-the-values approach that settled the HUD crop and this. If the flats are correct too, the
remaining suspect is draw order between flats and translucent sprites.

Worth noting the shape of this: three separate theories about the *sprite* path — missing clip
adjustment, depth bias, wrong z values — and the measurement showed the sprite path was never the
problem. Two of those were filed as work items before being tested.

**Flats verified correct too.** The core hook computes `plane.plane.ZatPoint(vt->fx, vt->fy) + dz`
(`gl_flats.cpp:255`), the same expression as the legacy immediate-mode path directly below it
(`:274`). So step heights are not the cause either.

State of the sprite truncation after eliminating everything reachable by inspection:

| Component | Verified |
|---|---|
| Sprite quad z extent | correct — `z2 == floorz` exactly, measured |
| Sprite quad winding | correct — strip `v1,v2,v3,v4` reordered to fan `v1,v2,v4,v3` covers the same quad |
| Flat vertex heights | correct — identical expression to the legacy path |
| Pass order | correct — opaque replayed before translucent |
| Depth bias | tried, made the metric worse, reverted |

Everything reachable by inspection is eliminated, so the next step must be a **runtime** comparison,
not more reasoning: draw the scene with sprites only (skip flats and walls in the core hook) and see
whether a zombieman renders whole. That separates "the sprite is drawn short" from "the sprite is
drawn whole and something covers it" — a distinction none of the work so far has actually tested,
and the one everything else hinges on.

If it renders whole, the occluder is real geometry and the question becomes which; if it is still
short, the truncation is inside the sprite draw itself despite the correct vertices, which would
point at the shader or the texture's own coordinates.

### RETRACTED: the sprites are NOT drawn whole — see below

Skipping flats and walls in the core hook and rendering sprites alone shows every zombieman
**complete** — full bodies, legs included. So the sprite draw is correct end to end and the
truncation is world geometry covering them.

Combined with the earlier measurements, that closes the sprite question entirely:

- the quad's bottom equals `floorz` exactly;
- the winding covers the full quad;
- rendered without world geometry, the sprite is whole.

The occluder is flats or walls whose emitted heights match the legacy expression exactly, so the
difference is not *where* the geometry is but *how the depth comparison resolves against it*.
Sprites are queued translucent — depth test on, depth write off — and replayed after all opaque
geometry. A sprite standing on a step is legitimately behind the step surface in front of it; the
legacy renderer draws sprites within its BSP-ordered lists rather than as one deferred pass, so it
never asks the depth buffer that question in the same way.

**The two-pass split is therefore the suspect** — it was added early to stop opaque geometry
depth-erasing sprites, and it may be what causes this instead. The test is direct: replay the scene
queue in insertion order rather than opaque-then-translucent, and see whether the legs return.

### Retraction and the real state of the sprite bug

The "drawn whole, then occluded" conclusion was **wrong**. It came from glancing at a sprites-only
capture at full-frame scale. Magnified, the sprite is plainly sliced along a vertical edge with the
right-hand portion missing — in a frame with no world geometry at all, so occlusion was never
possible. Reading a 800x600 screenshot at a glance is not verification; the user zoomed in and the
defect was obvious.

Magnified comparison of the same zombieman:

- **compat**: complete figure, correctly lit
- **core**: left portion only, hard vertical cut, and noticeably darker

Measured UVs for a sprite quad:

    ul=0.703  ur=0.000  vt=0.000  vb=0.859
    v1=(183,111)  v2=(138,111)  v3=(183,56)  v4=(138,56)

Those are power-of-two padding ratios (45/64, 55/64), and the legacy uploader pads via
`GetTexDimension`. `CreateTexBuffer` was uploading unpadded, so it now pads to match — **that is
correct on its own terms but did not fix the slice** (25.3477% → 25.3460%, and the magnified sprite
is unchanged). Kept because it matches the legacy uploader, not because it fixed anything.

A hard-edged cut through a quad means part of the geometry is not rasterised. What has *not* been
tested: whether both triangles of the fan actually draw. `Renderer2D::BeginScene` disables culling,
so a backwards winding should not matter — but that is reasoning, and reasoning has been wrong on
this bug five times now. **The test is to draw the two triangles in different solid colors and look
at which survives**, which distinguishes a missing triangle from a texture-coordinate problem in a
single run.

The sprite is also darker than compat, which is a second, probably separate defect and should not be
conflated with the slice again.

### Geometry eliminated: both triangles rasterise

Tinting triangle 0 red and triangle 1 blue and magnifying shows **both present**, forming a complete
quad with the diagonal seam visible between them. The fan reorder is correct, the winding is correct,
culling is not discarding anything, and the quad covers its full extent.

So the slice is in **texture sampling**, not geometry. That is now established by measurement rather
than argued, after five wrong theories on this bug.

What is known about the sampling:

- the quad is 45 x 55 world units, matching the sprite's own dimensions;
- the UVs are `u` 0.703 → 0.000 and `v` 0.000 → 0.859, i.e. the power-of-two padding ratios;
- `CreateTexBuffer` now pads to `GetTexDimension`, matching the legacy uploader, and the slice
  survives that.

So either the texture actually bound is still an unpadded upload — the adopt path and the
create-branch may disagree about which texture object the id refers to — or the `u` range is being
applied to a texture whose contents do not sit where those coordinates expect.

**The direct test is to dump the bound texture.** Read it back with `glGetTexImage` right after the
sprite's `BindTexture` and write it to a PNG. That shows the actual pixels and dimensions the sampler
sees, which settles padded-vs-unpadded and content-placement in one run, with no inference.

**Texture dump attempted, needs one more iteration.** Probing `GLBackend::BindTexture` reports the
first bound textures as `32x16` — power-of-two, so padding is working for those — but the probe fires
on the first binds of the frame, which are not sprites. It needs a filter (dump only when the bound
texture's dimensions are those of a sprite, or thread the `FTexture` through) to catch the one that
matters.

That is the immediate next step and it is small: filter the dump to a sprite bind, write the pixels
to a PNG, and look at what the sampler actually sees. Padded-vs-unpadded and content placement are
then both visible at once.
