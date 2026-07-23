# The renderer staircase (`feature/renderer-staircase`)

**The reset and the new strategy, decided 2026-07-23.** The hybrid port (`feature/hwrender-port`,
kept as the archived record) proved that every seam between our renderer and an adopted one is a
defect factory — fifteen of them are cataloged in its history. This branch goes the other way:
**one renderer at all times, upgraded by replaying upstream's own evolution** from our anchor
(GZDoom ~1.8, commit `ad88cfc5e`, 2013-12-25 — a near-exact structural match to our `src/gl/`)
forward, one coherent batch at a time.

## Why this works

- Every intermediate state was shipped and played upstream — a tested staircase, not a design.
- There is never a second pipeline, a capture, an adapter, or a convention to re-derive.
- **The history is small**: only **261 `src/gl` commits** between our anchor and 2016.
- **The staircase is fixed-point-native the whole way**: upstream modernized the GL renderer
  (GLEW/GL2 floor → render-state consolidation → VBO walls/flats → GLSL 1.3 floor → GZDoom 2.x
  shader-only renderer) *before* the 2016–17 float-sim conversion. Every step through ~GZDoom 2.1–2.4
  was written against a fixed-point sim. Our fixed64 widening is audited per batch (the strong
  `zx::Fixed` type catches 32-bit assumptions at compile time — proven twice already).

## The wall, and the target

The float-sim conversion (2016–17) is where verbatim replay must stop — after it, upstream renderer
commits assume `DVector` positions. **The target of this staircase is GZDoom ~2.1's renderer**:
GL 3.3, shader-only, buffer-based, core-profile-capable — everything the modern port wanted, still
fixed-point-native. Beyond that we reassess from a coherent base (the modern `common/rendering`
backend is game-model-free; the vendored reference trees stay in-repo for that day).

## The first flights of stairs (mined from history, oldest first)

1. **GL 2.0 floor + extension cleanup** (`69af73d9b`, `94b06900c` era): drop pre-GL2 paths; loader
   modernization. (We keep GLEW; the point is deleting the pre-2.0 fallbacks.)
2. **Lighting/state consolidation** (`978ace241`, `c47c7421a`, `98cc7eeb9`, `7793bbbcc`): all
   lighting through the 3 render-state parameters; **route every glColor through the render state**;
   remove the colormap parameter plumbing. This is upstream doing, properly and internally, what the
   hybrid's capture seam faked — the single highest-value batch.
3. **Uniform-based state** (`53f4cd010`, `52056a05b`): objectcolor/dynlight as uniforms.
4. **VBO geometry** (`7d3beb665`, `f7404d20f`, `b09405a8b`): vertex-buffer drawing for all walls and
   flats; glow via uniforms instead of extra attributes. (Kills the immediate-mode heart — and the
   macOS subsector-facet artifact this whole effort started from.)
5. **GLSL 1.3 floor** (`09f407143`): remove pre-1.3 shader compromises.
6. Continue batch-by-batch toward the GZDoom 2.x shader-only renderer (~200 remaining commits,
   grouped the way upstream grouped them).

Each batch: cherry-pick/adapt onto our tree → build (all platforms via CI) → tests green → **manual
end-to-end pass by the user on the live game** (the verification standard) → commit → next.

## Scope: what rides along, measured (fonts, textures, status bar, menus)

The window (anchor -> 2016) holds **2,207 upstream commits total**; the staircase's 244 `src/gl`
commits are remarkably self-contained -- **only 5 of them also touch shared systems** (notably
`105001301` "major cleanup of the texture manager"), and commit-scoped cherry-picks carry those
files automatically.

- **The GPU side of fonts, textures, the status bar and menus IS the staircase**: they draw through
  the 2D path and upload through `gl/textures/*` (material/hwtexture/translate), all inside the 244.
  They are clients (measured in the portability scope, sect. 12) -- they come along at every step.
- **Their game-side evolution is a small parallel track: 77 commits** in-window touching shared
  systems without touching gl -- textures 28, status bar ~24, menu 18, r_data 13, v_video 7,
  v_draw 5, fonts 4. Optional companion flights, not prerequisites; the texture-manager cleanup is
  the one worth taking early since later gl work assumes its shape.
- **Deliberately never taken** (post-wall): ZScript menus/statusbar (2017+), the FGameTexture split
  (2018+), unicode fonts (2019+). Our C++ MENUDEF and SBARINFO stay, as scoped.

## Where the staircase lands, version-wise

The summit (`fc0cf4f99` -> GZDoom 2.x) is **OpenGL 3.3 core minimum, 4.1 core on macOS** (Apple's
ceiling; the 4.1 -> 4.0 -> 3.3 request chain in `glcontext_compute` is built for exactly this).
GL 4.5+/Vulkan-class backends are post-wall; the vendored reference trees exist for that
reassessment.

## What this branch keeps from the port effort (banked, verified)

- **Native SDL2** (compatibility-only context for now — 3.0 → 2.1 via the tested
  `glcontext_compute`; the core request returns when a staircase step can use it). Legacy renderer
  verified unregressed on it. `sdlvideo.cpp`/`SDLMain.m`/Xcursor hack deleted; SDL2 color cursors.
- **All-platform CI** fixes; Linux `libsdl2-dev`.
- **`features/hwrender/computation/`**: the pure, 100%-covered units (fixed64-safe `vertexconvert`,
  `glcontext`, `backendselect`, matrix/frustum/etc.) — the staircase's tested-unit discipline.
- **Vendored reference trees** (`rendering/`, `ZVulkan/`, pinned `7bfbf61`) — inert, for the
  post-staircase reassessment; ZVulkan builds behind `HAVE_VULKAN` (off by default).
- **The docs**: the portability scope, the seam catalog, and the plan logs — the map of every trap.

## What was dropped

Every line of hybrid renderer code: the capture/queue seam, the adapted backend copy, the scene
bridge, `vid_hwrender` and its menu entry (they return when there is a second renderer to select),
all hook edits in `gl/`. The `gl/` tree is byte-identical to `main` again.

## Known temporary regressions (tracked, grep `ZX_TODO_FIXEDCOLORMAP`)

- **Per-actor FixedColormap on other players** (the [TP/BB] doom-sphere-renders-red-to-observers
  feature): upstream `c47c7421a` removed `FColormap::colormap`, the field that carried the
  per-sprite special-colormap index. The two set-sites in `gl_sprite.cpp` are disabled with
  `ZX_TODO_FIXEDCOLORMAP` markers. Restore via a per-object fixed-colormap render-state call once
  the shader-consolidation flights add one (upstream grows `SetFixedColormap` on the render state
  on the way to the core flip). Offline play is unaffected — the feature only triggers on other
  players' powerups in netgames.

## Skipped as inapplicable (not regressions)

- `03d4f23a6`, `d925279be`, `a26fbc74f` (May 2014): GL-side adaptations to ZDoom's
  long-texture-names change (removal of the 8-char lump-name limit). Zandronum never took the
  base-engine feature, so there is nothing for these to adapt; the pre-change texture-name code
  paths remain correct here. Revisit only if long texture names are ever backported wholesale.
