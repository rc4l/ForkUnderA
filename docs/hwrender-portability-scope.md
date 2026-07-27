# UZDoom renderer portability — scope

**Question asked:** how modular/portable are UZDoom's OpenGL/Vulkan renderer upgrades, and can we
track them *without* dragging in ZScript, the float sim, or the modern texture model? Anchor at the
UZDoom commit closest to the renderer we ship, then work up.

**Answer, up front:** UZDoom's renderer is split into a **backend half that is completely free of
ZScript and the game model** (`src/common/rendering/**`, ~34.6k lines, measured **0** references to
`AActor` / `sector_t` / `seg_t` / `player_t` / `VMFunction` / `PClass`) and a **scene half that is
welded to them** (`src/rendering/hwrenderer/scene/**`, ~19k lines). The jank is entirely on the scene
side, and it is the side we already keep our own copy of. Tracking upstream GL/GLES/Vulkan work means
tracking the backend half, which is genuinely portable — **with one real exception: the texture
model.** The backend is coupled to `FGameTexture`/`FMaterial`/`FHardwareTexture` (it must, to upload
and bind), and UZDoom's texture model differs structurally from ours. That is the one seam the port
cannot adapt-and-ignore; see §12. (Fonts, the status bar, and translations are *not* new coupling —
they are clients of textures and the 2D path.)

Method: measured against the local clone at `/Users/talhataj/repos/UZDoom` (trunk `7bfbf61`,
`5.0.0-pre`, 2026-07-18), full history unshallowed. All counts below are `grep -rl` file counts over
the current tree unless a commit is named.

---

## 1. Where we sit — the closest similar commit

Our renderer (`src/zandronum/src/gl/`) is a near-exact structural match to GZDoom's monolithic
`src/gl/` tree of **late 2013**. At UZDoom commit `ad88cfc5e` (2013-12-25) the subdirectories are
identical:

```
UZDoom @ 2013-12-25   api data dynlights hqnx        models renderer scene shaders system textures utility  gl_builddraw.cpp gl_functions.h
ZandroX (today)       api data dynlights hqnx hqnx_asm models renderer scene shaders system textures utility  gl_builddraw.cpp gl_functions.h
```

The only structural difference is our extra `hqnx_asm` (a Zandronum addition). This is the
GZDoom ~1.8 era: a **GLSL 1.20 compatibility-profile shader path grafted onto the fixed-function
pipeline**, exactly as `hwrender-PLAN.md` describes. That commit is our "you are here."

Everything upstream did to the renderer since then is the delta we're scoping.

## 2. The epoch spine (our base → UZDoom trunk)

Working up from our anchor, the renderer went through four architectural moves. Each is a single
identifiable commit range, not a diffuse rewrite:

| When | Commit | GZDoom ver | Move |
|---|---|---|---|
| 2013-12 | `ad88cfc5e` | ~1.8 | **our base** — monolithic `src/gl/`, fixed-function + GLSL 1.20 |
| 2018-04-02 | `60aebff4a` | 3.x | **the seam is born** — "starting separation of hardware dependent and hardware independent code." The `hwrenderer` abstraction (`FRenderState` interface + data buffers) splits out from the GL code. |
| 2019-02-20 | `c6b29846d` | 4.x | **Vulkan backend added** behind that seam — RAII wrappers, swapchain, stubbed then filled in. No change to the scene layer's API. |
| 2020-04-26/29 | `02832297f`, `652712d97` | 4.4pre | **backends moved to `common/rendering/`** — OpenGL, then Vulkan + softpoly. This is the tree layout UZDoom still uses. |
| 2021-08-03 | `441cd0796` | 4.x | **GLES backend merged** — a *third* backend behind the same seam, proving the abstraction generalizes. |
| 2026-07-18 | `7bfbf61` (HEAD) | 5.0.0-pre | UZDoom trunk |

The important thing this spine shows: **the expensive, risky work (the hw/hardware-independent
separation) is already done upstream and is 8 years old.** We are not proposing to build the seam;
we are proposing to adopt a seam that has since had three backends hung off it.

## 3. The portability verdict, by layer

Measured on current trunk. "Coupling" = number of files referencing the symbol.

### Portable — `src/common/rendering/**` (adopt wholesale)

| Subsystem | Lines | Files | `AActor` | `sector_t` | `VMFunction`/`PClass` | `FGameTexture` |
|---|--:|--:|--:|--:|--:|--:|
| `gl` | 7,297 | 24 | 0 | 0 | 0 | 0 |
| `gl_load` | 4,588 | 4 | 0 | 0 | 0 | 0 |
| `gles` | 7,736 | 27 | 0 | 0 | 0 | 0 |
| `vulkan` | 7,427 | 40 | 0 | 0 | 0 | 0 |
| `hwrenderer/data` | 4,624 | — | 0 | 0 | 0 | 3 |
| `hwrenderer/postprocessing` | 2,664 | — | 0 | 0 | 0 | 0 |
| **total** | **~34,600** | | **0** | **0** | **0** | **3** |

This half has **no knowledge of the game at all.** It does not know what an actor, a sector, or a
line is; it never calls the script VM. It speaks in vertices, buffers, textures, uniforms and draw
commands. The only leak into game-adjacent types is `FGameTexture` in **3 files** of
`hwrenderer/data` — the same adapter surface `hwrender-PLAN.md` already bridged (issue #4). Vulkan
and GLES come free here: they are just two more directories behind the identical interface.

### Not portable — `src/rendering/hwrenderer/scene/**` (keep ours)

~19k lines, and this is where every coupling lives:

- `sector_t` — **24 files** (`hw_bsp`, `hw_walls`, `hw_flats`, `hw_sprites`, `hw_portal`, `hw_sky`,
  `hw_fakeflat`, `hw_renderhacks`, …). This is the BSP/level walker; coupling to level geometry is
  its whole job.
- `AActor` — **10 files**; `seg_t` — **10 files**.
- `DVector` / `DAngle` (the **float sim**) — **14 files**. This is the deterministic-netplay killer,
  not ZScript: the scene reads floating-point actor positions and angles. Zandronum's sim is
  fixed-point, so this layer cannot be adopted without breaking demo/netcode determinism.
- **ZScript is the *shallowest* coupling of all** — `VMFunction`/`PClass` appear in exactly **one
  file**, `hw_weapon.cpp`, and only to call the weapon-bob script virtual (`ModifyBobLayer`). The
  "ZScript jank" fear is real but tiny and localized; the actual blocker to taking the scene layer is
  the float sim and the level-struct coupling, which are pervasive.

So the guidance in `hwrender-PLAN.md` holds and is now re-measured on trunk: **port the backend,
retarget our own scene walker to emit into it.** We keep the layer that knows our fixed-point level
data; we adopt the layer that doesn't care.

## 4. What "working your way up" costs after adoption

Because the seam has been stable since 2018 and the backends are game-agnostic, pulling a *later*
UZDoom renderer improvement is a directory-scoped cherry-pick, not a rebase of our fork:

- A GL/Vulkan/GLES fix upstream touches `common/rendering/<backend>/` and, rarely,
  `common/rendering/hwrenderer/data/`. Those files have no merge surface with our game code, so they
  update cleanly against our vendored copy.
- Anything upstream does in `rendering/hwrenderer/scene/` is **ours to reimplement or ignore**, by
  construction — it's the half we didn't take. That's a cost, but it's the same cost whether we adopt
  now or later, and it's bounded to features we actively want (e.g. `PerformSpriteClipAdjustment`,
  the full `DrawParms` 2D semantics — both already flagged in the plan).

The risk that *does* grow over time: the infrastructure headers the backend leans on (`v_video.h`
/ `DFrameBuffer`, `filesystem.h`, `printf.h`, `i_time.h`, the `FGameTexture` accessors) drift
upstream. Those are the ~9 shim points P1 already identified. They are shim/rename work, not the
game model, so they stay tractable — but a newer snapshot means re-checking them.

## 5. Structuring for pluggable backends (the `features/computation` ask)

**UZDoom already is the pattern we want.** Three live backends — GL, GLES, Vulkan — sit behind one
`hwrenderer` abstraction (`FRenderState` + the data buffers). Adopting it gives us the multi-backend
seam for free; we don't design it, we inherit it. The mapping onto our `features/` + `computation/`
convention:

```
src/zandronum/src/features/render/
  computation/                 # pure, tested units — the Phase-0 survivors + more
    matrix, frustum, vertexformat, batch, renderstate, quad, ...   (already 100% covered)
   irenderbackend.h            # the seam (our name for UZDoom's FRenderState boundary)
  gl/                          # backend 1  — vendored common/rendering/gl + gl_load
  vulkan/                      # backend 2  — vendored common/rendering/vulkan (later, ~free)
  gles/                        # backend 3  — vendored common/rendering/gles (optional, mobile/GL2)
  scene_bridge/                # OURS: retargets our gl/scene walker onto the seam
```

- The **backend directories are drop-in** — one per rendering API, each self-contained and
  game-agnostic, exactly as they are upstream. Adding Vulkan later is a new subfolder, not an engine
  change (Phase E in the plan).
- The **`computation/` units stay pure and tested** — matrix/frustum/vertexformat/batch/etc. are the
  math the seam needs and are the natural home for the "100% coverage" discipline this repo already
  applies to `computation/`.
- The **one file we write and own** is `scene_bridge/` — the thing that walks *our* fixed-point
  `sector_t`/`seg_t` and emits into whichever backend is selected. That is the deliberate boundary
  between "ours" and "theirs," and it's the only place the two worlds touch.

This is precisely what lets us "try other renderers in the future": a second backend is a sibling
directory behind `irenderbackend.h`, and the scene bridge and `computation/` units don't move.

## 6. Bottom line

- **Portable?** Yes — the GL/GLES/Vulkan backends (`common/rendering/**`, ~34.6k lines) are provably
  free of ZScript, the float sim, and the game model. That's not a hope; it's 0/0/0 on the greps.
- **ZScript jank?** Almost none in the layer we'd take. What jank exists is in the scene layer
  (`hw_weapon.cpp`, one file) — which we don't take anyway.
- **The real boundary** is the **float sim + level-struct coupling** in the scene layer, which is why
  we keep our own scene walker. This is a determinism/netcode line, not a ZScript line.
- **Pluggable backends** are the upstream design, not something we invent — adopting the seam gives us
  GL now and Vulkan/GLES as later drop-in subfolders under `features/render/`.

## 7. Menu options — separate jank, cheaply avoided

The renderer options menu is a **different portability question** from the renderer itself, and it
lands well for us:

**The two menu engines differ, but the data format is identical.**

| | Widget engine | `menudef.txt` |
|---|---|---|
| **UZDoom** | **ZScript** — `OptionMenuItem*` classes in `wadsrc/static/zscript/engine/ui/menu/optionmenuitems.zs` | text DSL |
| **ZandroX** | **C++** — `src/menu/{menudef,optionmenu}.cpp`, `optionmenuitems.h`; **no ZScript menu classes exist** | text DSL, same syntax |

So the menu *definitions* (`Option "$LABEL", cvar, ValueList`, `Slider`, `Submenu`, `OptionValue`
lists) use the **same DSL** in both trees — a line pasted from UZDoom's `menudef.txt` is parsed by
our C++ parser. What we must **not** adopt is UZDoom's ZScript menu *engine*; ours already renders
that format. **This is the same verdict as the renderer: take the data, not the ZScript machinery.**

**What porting a renderer option actually is** (per cvar, no ZScript):

1. make sure the ported backend reads the cvar (many already exist — `gl_mirror_envmap`,
   `gl_seamless`, `gl_spriteclip`, `gl_render_precise` are live in `gl/system/gl_menu.cpp`);
2. register any new cvar in C++ (`CVAR(...)` in `gl_menu.cpp`, exactly as today);
3. add the `Option`/`Slider` line to our `menudef.txt`;
4. add its `OptionValue` list if new (e.g. `LightingModes`, `FogMode`, `BillboardModes`).

**Backend selector.** We already have the precedent: `vid_renderer` (`sdl/hardware.cpp:80`,
`Int`, 1=GL/0=software, currently pinned to 1 since software was dropped). A backend picker is one
new archived int cvar (the plan's `vid_hwrender` — legacy-GL / core-GL / later Vulkan) plus one
`Option "Renderer backend", vid_hwrender, "RendererBackends"` line and a 2–3 entry `OptionValue`
list. UZDoom's own picker is `vid_preferbackend`; we don't need theirs. Note today's `VideoOptions`
page (`menudef.txt:708`) is a flat "DISPLAY OPTIONS" list with **no dedicated OpenGL submenu** — the
natural home is a new `OptionMenu "OpenGLOptions"` submenu mirroring UZDoom's, populated only with
cvars our backend honors.

**The one real catch — extended option syntax.** UZDoom's newer lines carry visibility args our
1.8-era parser handles only partially:

- UZDoom: `Option "...", cvar, values, gatecvar, gateval, HideInv` (gate on a *value*, then
  **Hide** the item).
- ZandroX: `FOptionMenuItemOption(label, menu, values, graycheck, center)` — a 4th-arg **graycheck**
  cvar that only *grays* the item, no gate-value, no Hide mode (`optionmenuitems.h:255`).
  `Submenu` with trailing gate args is likewise a newer form.

So UZDoom menu lines using the extended visibility/gating form must be **trimmed to our arg shape**
(drop the gate value + Hide mode, or accept gray-out) — a mechanical per-line edit, not a parser
rewrite. Plain `Option`/`Slider`/`OptionValue` lines (the majority) copy over verbatim.

**Menu bottom line:** no ZScript, no engine port. It's `menudef.txt` text edits + C++ cvar
registrations in the style the tree already uses, plus trimming the handful of extended-syntax lines.

## 8. Build system — wiring the backends, and what goes away

**Neither backend needs a system SDK — both loaders are vendored.**

- **OpenGL:** the loader is `common/rendering/gl_load/` (glad-style, in-tree). No GLEW, no external
  GL SDK. Wiring it = adding the `common/rendering/{gl,gl_load,hwrenderer}` source globs to CMake.
- **Vulkan:** one self-contained vendored library, `libraries/ZVulkan/`, which bundles **volk** (the
  Vulkan loader), **vk_mem_alloc** (allocator) and **glslang** (the GLSL→SPIR-V compiler) in its own
  `src/`. **No LunarG Vulkan SDK is required to build.** UZDoom's CMake gates it behind
  `option(HAVE_VULKAN ON)`: `add_subdirectory(libraries/ZVulkan)`, link `zvulkan`, and a separate
  `VULKAN_SOURCES` list "because it needs to be disabled for some platforms" — i.e. Vulkan is already
  a clean on/off toggle upstream.

So "is it easy?" — **the modularity is upstream's, not something we build.** GL is source-globs only;
Vulkan is one subdirectory + one define + one source list. Both are game-agnostic (§3), so they add
to the build without touching the sim.

**The bounded CMake work** (`src/zandronum/CMakeLists.txt`):

1. add the `common/rendering/**` globs (GL + hwrenderer + gl_load), plus shader lumps to the pk3;
2. **swap the GL loader GLEW → `gl_load`** (the plan already notes `__glewGenerateMipmap` unresolved
   under core — the vendored loader resolves the core entry points GLEW doesn't);
3. **migrate the SDL 1.2 API → native SDL2** — required for a core/Vulkan-capable context anyway
   (plan Phase B / P2), so it's not extra cost;
4. Vulkan later: `add_subdirectory(ZVulkan)` + `HAVE_VULKAN` + `VULKAN_SOURCES`.

**What goes away** (build shrinks, not grows):

| Removed | Why | Where |
|---|---|---|
| `sdl12-compat` | native SDL2 replaces the 1.2 shim | `mac_compile.sh` stops building `sdl12-compat-1.2.68`; the SDL 1.2 dylib is dropped |
| **GLEW** (Homebrew dep) | vendored `gl_load` is the loader | `mac_compile.sh` pkg list, CMake link |
| Legacy `#version 120` shaders + fixed-function branch + `gl_SetTextureMode` + 52 `gl.shadermodel` sites | one modern core path replaces them | plan Phase D / P4 |
| Software renderer | already dropped (GL-only build) | `menudef.txt:1877` comment; sim/render split done |
| Windows D3D9 / DDraw path (`fb_d3d9.cpp`, `fb_ddraw.cpp`) | dead since `vid_renderer` pinned to 1 | plan "out of scope" list |
| Bespoke `gl_backend.cpp` / `renderer2d.cpp` scaffolding | superseded by the vendored backend | plan P4 |

Net: we add ~3 vendored source trees (GL) plus one optional self-contained lib (Vulkan), and we
delete two external dependencies (`sdl12-compat`, GLEW) and the whole legacy fixed-function path.

## 9. What happens to Zandronum's *existing* menus

**Nothing structural — the menu system is renderer-agnostic.** All ~30 menus
(`OptionsMenu`, `CustomizeControls`, `HUDOptions`, `Scoreboard`, `Gameplay`, `Compatibility`,
`Sound`, `Multiplayer`, `Browser`, load/save, episode/skill, …) are C++ MENUDEF definitions whose
widgets issue **2D draw calls** — `screen->DrawTexture(...)` for every glyph/patch and
`screen->Dim(...)` for the darkened backdrop. They never call the 3D scene renderer. So the port
neither rewrites nor loses any menu; they keep their definitions and their C++ classes verbatim.

What the backend swap actually touches, concretely:

| Menu surface | Draw path | Consequence under the new backend |
|---|---|---|
| Every option/list menu (the ~30) | `screen->DrawTexture` | **Draws unchanged once the 2D path is wired** (plan's `DrawParms`/`F2DDrawer` work). The same 2D defects the plan tracks — text offsets, render style, clip — apply identically to menu text/graphics; fixing them for the HUD fixes them for menus. |
| **Menu background dim** (`menu.cpp:963` `screen->Dim`) | `DCanvas::Dim` | **`Dim` is one of the 2D primitives the plan lists as NOT yet hooked under core** (`Dim`/`Clear`/`FlatFill`/`FillSimplePoly`). Until hooked, menus lose their darkened backdrop — a concrete, visible regression to watch for. One hook, same shape as `DrawTextureV`. |
| Player-setup preview (`playerdisplay.cpp`) | `FBackdropTexture::Render()` → CPU-generated texture → `screen->DrawTexture` | Not a 3D render — it bakes a backdrop into a texture and blits it. Works once `DrawTexture` + texture upload work (already done in the plan). |
| **VideoModeMenu** (`videomenu.cpp`, `ScreenResolution` widgets, "test mode 5s") | queries/sets video modes through the SDL path | **Definition unchanged; its backing code moves with the SDL 1.2 → SDL2 migration** (Phase B). Mode enumeration and set go through SDL2; the destroy-and-recreate on every mode change is replaced by in-place resize. This is the one menu whose *C++* is genuinely affected, and only because it drives video setup. |

**Stale entries surfaced (optional cleanup, not caused by the port):**

- `"Column render mode"` → `r_columnmethod` (`VideoOptions`) is a **software-renderer** cvar — already
  a no-op in the GL-only build. Prune candidate.
- The software-renderer selector and DirectDraw entries are **already removed/commented**
  (`menudef.txt:1877`, `:18-19`).
- `r_drawfuzz` / `r_stretchsky` / `r_fakecontrast` are software-era cvars the GL path may ignore —
  audit whether each still has an effect before keeping its menu line.

**Additions (small):** a backend selector is one `Option "Renderer backend", vid_hwrender, …` line in
`VideoModeMenu`/`VideoOptions` plus a value list (§7), and — if we mirror UZDoom — a new
`OptionMenu "OpenGLOptions"` submenu for the renderer cvars our backend honors.

**Net:** the existing menus survive as-is. The only real work is (1) hook `Dim` (and the other
unrouted 2D primitives) into the new backend so backdrops draw, and (2) let `VideoModeMenu`'s video
code ride the SDL2 migration it needs anyway. Everything else is optional pruning and additive lines.

## 10. Incidental fixes the upgrade brings (gamma-on-crash, borderless, …)

Two different stories here — one is fixed *by construction*, the others are *capabilities* that
arrive but still need a line of wiring.

### Gamma reset on crash — fixed by construction (better than the plan's "deferred" framing)

`hwrender-PLAN.md` files this as deferred, needing a crash handler plus the tested
`gammaramp_compute`. **Adopting UZDoom's renderer removes the bug's cause entirely, so that workstream
becomes unnecessary for this symptom.** The reason:

| | How brightness is applied | Crash behavior |
|---|---|---|
| **ZandroX now** | **hardware gamma ramp** — `SDL_SetGammaRamp` (`sdl/sdlglvideo.cpp:390`), saving/restoring the desktop ramp in the framebuffer dtor | ramp restore never runs on signal death → **desktop left washed out** |
| **UZDoom** | **post-process shader** — `shaders/pp/gamma.fp` `ApplyGamma()` folded into the `present.fp` pass (`pow(val, InvGamma)`); **no `SetGammaRamp` anywhere in the renderer** | the hardware ramp is never modified → **nothing to leave corrupted; the bug cannot occur** |

Once brightness runs through the present shader (as it must under core), we stop touching the thing
that gets left in a bad state. No crash handler, no ramp save/restore, no `gammaramp_compute`.
**Caveat:** this only holds after the shader-gamma present pass is the one on screen — during the
transition, any frame still presented by the legacy `SetGammaRamp` path keeps the old behavior. So
it's fixed when the legacy present path is retired (plan P4), not the moment the port lands.

### Borderless / windowed-fullscreen, in-place resize, Linux vsync — capability free, option is a wire-up

These ride the **SDL 1.2 → SDL2 migration** the core context needs anyway. SDL 1.2 "cannot request"
modern window flags; SDL2 exposes `SDL_WINDOW_BORDERLESS` / `SDL_WINDOW_FULLSCREEN_DESKTOP`,
`SDL_GL_SetSwapInterval` (Linux vsync), and in-place resize (replacing the destroy-and-recreate on
every mode change). The plan is explicit that these "come for free but are not goals":

- **In-place resize** and **Linux vsync** are automatic — they're just how the SDL2 path behaves.
- **Borderless / windowed-fullscreen** needs the small user-facing piece: a **tri-state video mode**
  (fullscreen / windowed / borderless) plus one `Option` line in `VideoModeMenu` (§9). The *plumbing*
  arrives with SDL2; the *toggle* is ours to add — a menudef line + a cvar, no ZScript, no renderer
  work.

**Net:** the SDL2/core upgrade **eliminates the crash-gamma bug outright** (no extra code) and
**delivers borderless/resize/vsync as capabilities**, of which borderless is the only one needing a
few lines of menu + cvar to surface.

## 11. Where the new files live (structure, tests, and `fixed_t`)

Three kinds of code, three homes — dictated by the repo's own conventions, not invented here.

**Convention recap (measured):** `tests/CMakeLists.txt` glob-recurses `*_compute.cpp` + `*_test.cpp`
under `src/zandronum/src/features/` with `CONFIGURE_DEPENDS` — a new unit + test is **auto-discovered,
no CMake edit**. The catch that shapes everything: a `*_compute.cpp` must be **pure** (no engine/GL/
SDL headers) so it links against GoogleTest standalone. Vendored libraries sit as **siblings of
`src/`** under `src/zandronum/` (`zlib`, `dumb`, `gdtoa`, …).

### 11.1 Vendored upstream backend — keep upstream's layout, sibling to the other vendored libs

Mirror UZDoom's `src/common/rendering/` paths verbatim so §4's directory-scoped cherry-picks stay
trivial. This is third-party code, so it does **not** go under `features/` (our code):

```
src/zandronum/rendering/            # vendored from UZDoom common/rendering — DO NOT hand-edit; re-pull
  gl/         gl_load/              # OpenGL backend + glad-style loader
  gles/                            # GLES backend (optional)
  vulkan/                          # Vulkan backend (gated HAVE_VULKAN)
  hwrenderer/  { data/ postprocessing/ }   # the abstract FRenderState seam + buffers
src/zandronum/ZVulkan/             # self-contained Vulkan lib (volk+vk_mem_alloc+glslang), sibling of zlib
```

Its filenames (`gl_renderstate.cpp`, `vk_renderstate.cpp`, …) carry no `_compute`/`_test` suffix, so
the test glob never touches it. Provenance (the trunk SHA it was pulled from) goes in a
`rendering/UPSTREAM.md` so re-pulls are diffable.

### 11.2 Our render feature — `features/hwrender/` (matches `freeform-menu`, `fixed64`, `openal-sound`)

```
src/zandronum/src/features/hwrender/
  README.md
  irenderbackend.h                 # the seam (our name for the FRenderState boundary; multi-backend point)
  hwrender_init.cpp                # brings the vendored backend up; owns the core-profile flag
  scene_bridge.{h,cpp}             # OURS: walks our fixed_t level data → emits into the backend  ← the fixed_t boundary
  zx_texbridge.{h,cpp}             # FTexture/FGLTexture → their FHardwareTexture adapter (§3's 3-file FGameTexture surface)
  zx_video.{h,cpp}                 # ZXFrameBuffer/ZXCanvas ↔ DFrameBuffer/DCanvas
  computation/                     # pure, 100%-tested — auto-globbed, no engine headers
    vertexconvert_compute.{h,cpp} + _test.cpp   # fixed_t(48.16, 64-bit) → float   ← see 11.3
    matrix_compute...  frustum_compute...  vertexformat_compute...
    batch_compute...   quad_compute...      renderstate_compute...
    glcontext_compute...        # ComputeGLContextRequest (core/compat fallback chain, plan P2)
    invertkeytable_compute...   # SDL2 scancode inversion (plan P2)
    backendprereq_compute...    # null-global guard order (plan)
    # gammaramp_compute — likely retired: §10 makes gamma a shader, not a hardware ramp
```

The split mirrors `fixed64/`: pure math in `computation/` (globbed + tested), engine-coupled glue at
the feature root (compiled by the engine's explicit source list, never the test glob).

### 11.3 The `fixed_t` widening — the one boundary that must be tested

We widened `fixed_t` to **64-bit 48.16** (`basictypes.h:105`, `typedef zx::Fixed fixed_t`). The
vendored backend is float/`DVector` internally, so the **only** place the two worlds meet is
`scene_bridge` → `vertexconvert_compute`, converting fixed-point level coordinates to render floats.
That unit is the highest-risk file in the whole port and gets the fixed64 treatment:

- **Must not assume 32 bits.** Convert through `zx::Fixed` / the existing `fixed64/computation/`
  units (`fixed_strong`, `dist_compute`), never a raw `x / 65536.0f` on a truncated int
  (see the `fixed64-widening` skill). Reuse those units; don't duplicate the math.
- **Tested past the old range.** Colocated `vertexconvert_compute_test.cpp` must feed coordinates
  beyond ±32767 map units (giant maps, far targets, non-cardinal angles — the exact fixed64 failure
  signature) and assert the float output, at 100% coverage like every other `computation/` unit.
- **Determinism is preserved by construction.** The conversion is one-way (sim → pixels); the float
  backend never writes back into the fixed-point sim, so netplay/demo determinism is untouched — the
  same reason §3/§6 keep our scene walker instead of UZDoom's float scene layer.

### 11.4 Build wiring summary

- `computation/` units + tests: **nothing to wire** — the glob finds them (must stay header-pure).
- Feature glue + vendored `rendering/`: added to the engine's explicit CMake source list in
  `src/zandronum/CMakeLists.txt` (§8).
- `ZVulkan`: `add_subdirectory(ZVulkan)` + `HAVE_VULKAN` (§8), only when Vulkan is turned on.
- A second backend later (Metal, software-poly) = a new sibling dir under `rendering/` behind
  `irenderbackend.h`; `scene_bridge` and every `computation/` unit are untouched.

## 12. Shared assets — textures, fonts, palettes, the status bar (thorough)

The recurring question "is X tightly coupled to the renderer?" has one answer for **textures** and a
different answer for everything that *uses* textures. Measured on both trees.

### 12.1 Textures — the ONE real seam (corrects §3's undercount)

§3 reported "`FGameTexture` in 3 files" — that was the `gl` subsystem only. Across the whole vendored
backend the texture-model coupling is broad, because a renderer must know how to upload and bind:

| symbol in `src/zandronum/rendering/` | files |
|---|--:|
| `FTexture` | 21 |
| `FMaterial` | 15 |
| `FHardwareTexture` / `IHardwareTexture` | 14 / 12 |
| `FGameTexture` | 9 |

**The mismatch is structural, not a rename.** UZDoom split the old monolithic texture into three
layers — `FImageSource` (decodes pixels) → `FTexture` (image + processing) → `FGameTexture`
(game-facing: scale, offsets, type, material list) — in `src/common/textures/` (which we did **not**
vendor). Our tree still has the GZDoom-1.x **conflated** `FTexture` (base, with `FWarpTexture`,
`FCanvasTexture` subclasses) plus its own `FMaterial` (`gl/textures/gl_material.h`). So:

- **Name collision:** our `FMaterial` vs their `FMaterial` (`common/textures/hw_material.h`).
- **Shape mismatch:** the backend calls `FGameTexture`-shaped accessors our `FTexture` lacks.

**Resolution — the `zx_texbridge` adapter (plan issue #4, "tech debt"):** keep our texture manager;
adopt our `FGLTexture`'s GL handle into their `FHardwareTexture`; and grow `FTexture` with the
accessors the backend reads — `GetTranslucency`, `GetShaderSpeed`, `GetGlossiness`,
`GetSpecularLevel`, `GetClampMode`, `GetTexture`, `isWarped` (enumerated in P1). Bounded, but this is
where the integration work concentrates.

**Two texture types are genuinely renderer-coupled and need attention:**

- **Camera / render-to-texture** (`FCanvasTexture`: security cameras, mirrors). The renderer draws
  *into* these. Ours is CPU-fed (`bNeedsUpdate` + `RenderView`); the backend has a hardware-canvas
  path (`isHardwareCanvas`, render-to-FBO). These must move from our CPU path to the backend's, or
  cameras render nothing under core.
- **Warp** (animated water/fire surfaces). UZDoom does warp **in a shader**
  (`hwrenderer/data/hw_shaderpatcher`); ours is a CPU `FWarpTexture` that regenerates pixels. Under
  the ported path the CPU warp becomes redundant — the shader does it — so `FWarpTexture`'s warp is
  dead weight, not a port blocker.

### 12.2 Fonts — NO new coupling (texture client + 2D path)

The vendored backend has **`FFont` in 0 files and `DrawChar` in 0 files** — it never sees a font. In
our tree `FFont::GetChar` returns an **`FTexture`** glyph (`v_font.h:82`), drawn via
`DrawChar`/`DrawText` → `DCanvas::DrawTexture` → the 2D path. So a font is just a producer of texture
glyphs that flow through the **same `zx_texbridge` adapter** as any texture, then draw through the
**same 2D path** as menus and the HUD. Nothing font-specific reaches the backend.

The only version wrinkle: UZDoom's `FFont::GetChar` returns `FGameTexture*` and takes a `translation`
argument; ours returns `FTexture*` with translation applied elsewhere. Because we **keep our own font
system** (it is game-side, not vendored), that difference stays internal to us — the glyphs still
just need the texture bridge. Fonts come along for free once textures + 2D work; they are not a
separate port item.

### 12.3 Palettes / translations / colormaps — shallow, shim-level

Backend coupling here is thin and via types both trees already share:

| symbol in `rendering/` | files | nature |
|---|--:|---|
| `PalEntry` | 7 | a 32-bit RGBA color — a fundamental shared type, compatible as-is |
| `FColormap` / `FSpecialColormap` | 2 / 2 | small structs (the `palentry.h`/`renderstyle.h` shims P1 added) |
| `FRemapTable` (sprite translations) | **0** | not in the backend — translations resolve game/2D-side to an id + texture, applied in the adapter/shader |

So sector lighting, fog, colormaps and translations reach the backend as small values (a `PalEntry`,
a colormap struct, a translation id), not as a subsystem to port. This is shim work, not a rewrite.

### 12.4 Status bar — 2D path, like menus (recap of §9)

`DBaseStatusBar` + the `SBARINFO` **text DSL** are pure C++ (`g_shared/sbarinfo.cpp`,
`shared_sbar.cpp`, `st_hud.cpp`); **zero ZScript status-bar classes**. Every element draws via
`screen->DrawTexture` — the same 2D path as menus and fonts, touching `fixed_t` in only 2 files. It
inherits the 2D-path work (hook `Dim`/`FlatFill`, honor the full `DrawParms`) and adds no renderer
coupling of its own.

### 12.5 Summary

| Asset system | Coupled to the backend? | What it needs |
|---|---|---|
| **Textures** | **Yes — the one real seam** | `zx_texbridge` adapter (issue #4); camera→hardware-canvas; warp becomes shader-side |
| Fonts | No | ride the texture adapter + 2D path; keep our `FFont` |
| Status bar / HUD | No | 2D path (hook `Dim`, `DrawParms`) — same as menus |
| Palettes / colormaps | Shallow | `PalEntry`/`FColormap` shims (basic shared types) |
| Translations (`FRemapTable`) | No | resolved game/2D-side to an id + texture |

**Bottom line:** textures are the tightly-coupled seam and where P1 integration concentrates;
fonts, the HUD, and translations are texture/2D-path clients that come along for free once that seam
and the 2D drawer are in place.

**Sources:** [UZDoom](https://github.com/UZDoom/UZDoom) · local clone `/Users/talhataj/repos/UZDoom`
@ trunk `7bfbf61`. Companion: `docs/hwrender-PLAN.md` (the staged execution plan this scoping backs).
