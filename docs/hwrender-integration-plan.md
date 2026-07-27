# hwrender — end-to-end integration plan (threading the coupling points)

How the coupling points from `docs/hwrender-portability-scope.md` §12 get handled in an order that
actually builds. This is the dependency-ordered version of `hwrender-PLAN.md`'s P1–P5, written around
the seams rather than the phases. Current branch state is in `docs/hwrender-branch-status.md`.

## The one organizing principle

**The texture bridge is the linchpin, and 2D is done before 3D.** Every visible thing — a wall, a
glyph, a HUD icon, a menu patch — is ultimately *a texture drawn as quads*. So the whole port funnels
through one adapter (`zx_texbridge`), and the cheapest place to prove that adapter works is the 2D
path (flat quads, no BSP, no lighting), which also happens to light up **fonts, the HUD, and menus
all at once**. Get textures + 2D right and three "coupling points" collapse into clients of one seam.

Dependency graph (→ = "must precede"):

```
  infra shims ─┐
               ├─→ texture bridge ─┬─→ 2D drawer ─→ (fonts, HUD, menus)
  FMaterial    ┘                   └─→ scene bridge ─→ (walls, flats, sprites) ─→ lighting/fog
  rename                                   ↑
  SDL2 + core context ─────────────────────┘
```

## Stage 0 — done on this branch

The pure, testable foundations are in and green (see branch-status): `vertexconvert_compute` (the
`fixed_t`→float boundary), `glcontext_compute` (context chain), `backendselect_compute`
(`vid_hwrender`), the vendored `rendering/` + `ZVulkan`, and the `vid_hwrender` cvar + OpenGL Options
menu. Nothing draws yet; the backend is dormant.

## Stage 1 — the seam: infra shims + FMaterial rename + texture bridge  ← the hard part

This is where the texture coupling (§12.1) is paid off, and it is the bulk of the work.

1. **Wire `rendering/` into CMake, compiling dormant.** Add the source globs; nothing calls it yet.
2. **Resolve the `FMaterial` name collision.** Rename our `gl/textures/gl_material` `FMaterial` →
   `LegacyFMaterial` (the same yield-to-upstream move the plan made for `LegacyFlatVertexBuffer` /
   `TM_*`), so the vendored `FMaterial` owns the name.
3. **Infra shims** (the ~9 P1 surfaces, all rename/shim not redesign): `zx_video`
   (`DFrameBuffer`/`DCanvas` ↔ `ZXFrameBuffer`/`ZXCanvas`), `filesystem.h`→our `Wads`, `printf.h`,
   `palentry.h`/`renderstyle.h` (the `PalEntry`/`FColormap` shims — §12.3), `i_time`, `cmdlib`.
4. **`zx_texbridge` — the adapter.** Grow `FTexture` with the `FGameTexture`-shaped accessors the
   backend reads (`GetTranslucency`, `GetShaderSpeed`, `GetClampMode`, `GetGlossiness`,
   `GetSpecularLevel`, `GetTexture`, `isWarped`); adopt our `FGLTexture`'s GL handle into their
   `FHardwareTexture`. Pull the pure decisions out as tested `computation/` units (the clamp-mode
   branching is already noted as `ComputeClampMode`; add `texbufferpad`/anything with a real rule).
5. **Null-global guard.** Bring `backendprereq_compute` in as the init check so a missing
   `zx_screen`/buffer fails with a named message instead of the crash-handler spin the plan hit.

**Done when:** the engine compiles and links with the backend present but dormant; new bridge
`computation/` units at 100%. No pixels yet. *(This is P1; it is large and the plan's debugging notes
are the map.)*

## Stage 2 — SDL2 + a real core context

Port `sdl/*` from the SDL 1.2 API to SDL2 (bundled already), feeding `glcontext_compute`'s chain into
`SDL_GL_SetAttribute`. `vid_hwrender` (Stage 0) selects the profile via `backendselect_compute`.
**Done when:** `vid_hwrender 1` creates a core context (legacy shaders fail to compile in it — that
failure *is* the proof it's core, per the plan). Borderless/in-place-resize/vsync come free (§10).

## Stage 3 — the 2D drawer  ← first pixels; fonts + HUD + menus light up together

Route `screen->DrawTexture` **and the currently-unhooked 2D primitives** `Dim` / `FlatFill` /
`FillSimplePoly` (§9, the menu-backdrop and HUD gaps) into the backend's 2D path, honoring the full
`DrawParms` (offsets, clip, translation, render style — the HUD-crop/inverted-font class of defects).
Because fonts and the HUD are just texture-quads (§12.2, §12.4), they come up here for free.

**Done when:** MCP A/B on a menu screen + the HUD shows parity — glyphs, patches, icons, the darkened
menu backdrop. This validates `zx_texbridge` on the simplest geometry before the scene depends on it.

## Stage 4 — the scene bridge  ← walls, flats, sprites

Retarget our fixed-point BSP walker to emit into the backend's `FRenderState`, **using
`vertexconvert_compute`** (done, tested) at every fixed→float crossing — the `fixed_t` coupling is
already solved, this is where it's consumed. Subsystem by subsystem, MCP A/B each: walls → flats →
sprites. The two renderer-coupled texture types (§12.1) are handled as they arise:

- **Camera / `FCanvasTexture`** (mirrors, cams): move from our CPU `RenderView`-into-canvas to the
  backend's hardware-canvas (`isHardwareCanvas`, render-to-FBO).
- **Warp**: drop the CPU `FWarpTexture` regen; the ported shader warps (`hw_shaderpatcher`).

**Done when:** MAP01 renders through the core path with wall/flat/sprite parity in the A/B metric.

## Stage 5 — lighting, fog, dynamic lights, translations

Sector lighting and fog through the shader uniforms; sprite translations resolve to an id + sampler
(§12.3, `FRemapTable` never enters the backend). Dynamic lights via the already-Core-clean
`FLightBuffer`.  **Done when:** the A/B metric closes to noise on a fixed scene set.

## Stage 6 — delete the legacy path (P4)

Only after Stage 5 parity: remove the fixed-function `FRenderState::Apply` branch,
`gl_SetTextureMode`, the 52 `gl.shadermodel` sites, the `#version 120` lumps, `sdl12-compat`, and the
bespoke `gl_backend`/`renderer2d` scaffolding. Our `LegacyFMaterial` and `FWarpTexture` CPU warp go
here too. One modern path.

## Stage 7 — Vulkan (P5)

Second backend behind the same seam. `ZVulkan` is vendored (Stage 0); wire `add_subdirectory(ZVulkan)`
+ `HAVE_VULKAN` + `VULKAN_SOURCES`, add the `win32vulkanvideo`/SDL Vulkan surface glue, and
`vid_hwrender 2` selects it via `backendselect_compute` (which already falls back to core GL when
unsupported). The scene bridge and every `computation/` unit are untouched — that is the payoff of the
seam.

## What threads through all of it — verification

- **Per stage:** MCP `launch_instance` → `screenshot`, A/B against the pre-migration image on a fixed
  scene set; the area-weighted metric for lighting/shading shifts, plus reading the image for
  small-area defects (icons, glyphs) the metric is blind to (§ the plan's method notes).
- **Continuously:** every pure decision extracted to a `computation/` unit at 100% (the tree's
  standard), and `backendprereq_compute` as the named-failure guard against null-global spins.
- **Determinism:** untouched by construction — the whole port is draw-only, downstream of the
  fixed-point sim; `vertexconvert` is one-way. Still smoke-test demo playback at Stage 4 and 6.

## File manifest — what is added, deleted, and kept-but-retargeted

Concrete paths (all under `src/zandronum/`). The important nuance: **most of our `gl/` tree is kept
and retargeted, not deleted** — we keep the code that walks our fixed-point level data and replace
only the immediate-mode submission layer.

### ADDED

| Path | What | Status |
|---|---|---|
| `rendering/` (150 files) | vendored UZDoom GL/GLES/Vulkan/hwrenderer backend | **on branch** |
| `ZVulkan/` (159 files) | vendored self-contained Vulkan lib | **on branch** |
| `src/features/hwrender/computation/` | pure tested units: `vertexconvert`, `glcontext`, `backendselect` (+ to add: `clampmode`, `texbufferpad`, `backendprereq`, matrix/frustum as needed) | **3 on branch** |
| `src/features/hwrender/` glue | `irenderbackend.h`, `hwrender_init.cpp`, `zx_video.{h,cpp}`, `zx_texbridge.{h,cpp}`, `scene_bridge.{h,cpp}` | Stage 1/4 |
| `wadsrc/static/shaders/hwrender/` | UZDoom's ~30 `#version 330` GLSL lumps | Stage 3 |
| `src/sdl/hardware.cpp` (edit) | `vid_hwrender` cvar | **on branch** |
| `wadsrc/static/menudef.txt` (edit) | OpenGL Options submenu + `RendererBackends` | **on branch** |

### DELETED (Stage 6, after parity — except SDL which is Stage 2)

| Path | Why |
|---|---|
| `wadsrc/static/shaders/glsl/` (18 lumps: `func_*.fp`, `fuzz_*.fp`, `fogboundary.fp`, …) | replaced by `shaders/hwrender/` `#version 330` set |
| `gl/renderer/` immediate-mode `FRenderState`/`FGLRenderer` (2.3k lines, 7 files) | superseded by the vendored backend |
| `gl/shaders/` legacy shader manager (1.7k, 4 files) | superseded by the vendored shader manager |
| `gl_SetTextureMode` + fixed-function bits in `gl/system/gl_interface.cpp` | folded into the shader `texturemode` uniform |
| our `FMaterial` (`gl/textures/gl_material.*`) | renamed `LegacyFMaterial` at Stage 1, deleted here; vendored `FMaterial` owns it |
| `FWarpTexture` CPU warp (`textures/`) | warp is shader-side (`hw_shaderpatcher`) |
| `sdl/sdlvideo.{cpp,h}`, `sdl/SDLMain.m` | software framebuffer (gone) + SDL2 supplies its own main (Stage 2) |
| `win32/fb_d3d9.cpp`, `fb_d3d9_wipe.cpp`, `fb_ddraw.cpp` | dead D3D9/DDraw path, unreachable since `vid_renderer` pinned to 1 |
| `sdl12-compat` build step in `mac_compile.sh` (+ Linux/Windows scripts) | native SDL2 replaces the 1.2 shim |
| the 52 `gl.shadermodel` conditional sites across 16 files | one modern path, hard-coded |

### KEPT + RETARGETED (the code that knows our fixed-point world — NOT deleted)

| Path | Role after the port |
|---|---|
| `gl/scene/` (14k lines, 21 files — walls/flats/sprites/portals/sky BSP walker) | driven by `scene_bridge`; emits into the vendored `FRenderState` instead of immediate mode |
| `gl/textures/` `FGLTexture` (minus `FMaterial`) | its GL handle is adopted into the backend's `FHardwareTexture` via `zx_texbridge` |
| `gl/dynlights/` `FLightBuffer` (already Core-clean TBO code) | wired to the backend's light path |
| `gl/models/` (model/voxel draw) | retargeted onto `FRenderState` |
| `gl/system/gl_menu.cpp` GL cvars | kept as-is (menu already points at them) |
| `sdl/` input/main/system (`i_input`, `i_main`, `hardware`, `sdlglvideo`) | **rewritten in place** to the SDL2 API (Stage 2), not deleted |
| `textures/textures.*` `FTexture`/`FTextureManager` | kept; `FTexture` grows the `FGameTexture`-shaped accessors (`zx_texbridge`) |
| whole `g_shared/` status bar, `v_font.*`, menus | untouched — 2D-path clients (§12.2/12.4) |

**Rough scale:** ~+310 vendored files + ~15 new `features/hwrender/` files; ~−40 legacy files (7
`gl/renderer` + 4 `gl/shaders` + 18 glsl + 3 win32 + 2 sdlvideo + `SDLMain.m` + `FMaterial`/warp);
~14k lines of `gl/scene` **kept**. The net is *more* code (the vendored backend), but our
hand-maintained surface shrinks — the immediate-mode + legacy-shader + dead-D3D9 code goes, and the
part we keep is the scene walker we understand.

## Critical path & risk

The risk is concentrated in **Stage 1** (texture bridge + `FMaterial` collision + infra shims) — it
is most of the effort and the plan's P1 notes are the record of how thorny it was. Stages 3–5 are
incremental and individually A/B-verifiable, so they are low-risk once Stage 1 stands. Stage 7 is
cheap because the seam pre-exists upstream. So: **spend the care on the texture seam; everything
after it is a verifiable march.**
