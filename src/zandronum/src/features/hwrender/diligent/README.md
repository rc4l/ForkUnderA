# features/hwrender/diligent — the renderer backend swap

Vendored backend: **DiligentCore** (`deps/DiligentCore`, Apache-2.0, rev `bb821b7`), Vulkan only.
Apache-2.0 is GPL-3 compatible, so this does not endanger the fork's licensing position.

**On by default** for every client build, on Windows, Linux and macOS (through MoltenVK); a dedicated
server builds without it. `-DFUA_DILIGENT=OFF` builds GL (Legacy) alone.

## Files

The backend's files share `dgscene_internal.h` and nothing outside them includes it. The scene's
state lives in four records there: `g_scene` (buffers, batches, the frame's dynamic runs), `g_pso`
(the world pipelines), `g_frame` (what a frame's passes share) and `g_stats` (counters for the reports).

| File | What |
|---|---|
| `dgscene.cpp` | the world pass: frame prep, `DrawWorld`, `DrawSceneOnce`, scene depth and constants |
| `dgpipelines.cpp` | the world pipelines and their material bindings: bindless array, slots, animated slots |
| `dgupload.cpp` | the scene buffer: the material-sorted build and its in-place patches |
| `dgdynamic.cpp` | sprites, models and decals each frame, and the sorted translucent pass |
| `dgviews.cpp` | line mirrors (raster and ray-traced), reflective planes, stacked sectors |
| `dgsky.cpp`, `dglights.cpp`, `dg2d.cpp`, `dgdecals.cpp`, `dghorizon.cpp` | sky and portals, dynamic lights, 2D, projected decals, horizon lines |
| `dgcamera.cpp` | camera textures |
| `dgframe.cpp` | the frame driver: level arming, the live frame, the present clock |
| `dgdiag.cpp` | benches, statistics, replay capture, screenshots and their console commands |
| `dgtexture.cpp`, `dgwindow.cpp`, `dgprobe.cpp` | textures, the swapchain, the device |
| `dgplatform.h`, `dgwin32.cpp`, `dgsdl.cpp`, `dgcocoa.mm` | each platform's window and the handles its Vulkan surface takes: an HWND; an X11 window or Wayland surface through SDL2; a CAMetalLayer view drawn through MoltenVK (`posix/cocoa/metalview.h`'s `CocoaMetalView`, shared with the engine's window), with the .app's own Vulkan loader |

## Status: TEXTURED world rendering on Sunder, through Vulkan

Sunder MAP10 renders through Diligent with the engine's own materials — 24,558 vertices, 4093 mesh
pieces resolved to **34 material batches and 34 uploaded textures**, verified by reading the
swapchain back to a PNG (`fua_diligent_shot`).

Textures come through `FMaterial::CreateTexBuffer`, which is the seam that keeps everything
Doom-specific — multipatch composition, translation, hires replacement — on the engine side. The
backend receives RGBA bytes and knows nothing else, which is exactly what makes it portable.

### The material-keyed vertex buffer

The first textured attempt produced **4093 draws and exhausted Vulkan's dynamic heap outright**,
because pieces sharing a material are scattered through the level mesh and each needed its own draw
plus its own constant-buffer map. Two changes fixed it:

* **the vertex buffer is laid out by material**, so each material is one contiguous run — 4093 draws
  become 34;
* **light is baked per vertex**, so the constant buffer is mapped once per frame rather than once per
  draw.

This is the same material-keyed layout the GL side could not profit from (`docs/levelmesh-PLAN.md`,
P2c/P2d) — but here it is not an optimisation, it is a correctness requirement.

### The present cost, and what it is not

`presented` is ~1.77 ms/frame while `submit-only` (same draws, GPU-synced, no present) is ~0.010 ms.
That gap is **swapchain present on a windowed 640x400 surface**, not draw work — proven by the
submit-only figure, which includes `WaitForIdle` and therefore all GPU time.

Two hypotheses were tested and both were wrong, which is worth recording so they are not retried:

* **the Win32 message pump** — pumping every frame vs every 60th changed nothing;
* **alpha-test defeating early-Z** — splitting opaque and masked into two pipelines changed nothing
  (still 1.77 ms) *and broke the image*, because `FMaterial::isMasked()` answers a GL-pipeline
  question rather than "does this texture have see-through texels", so materials routed opaque
  painted their transparent pixels over detail behind them. Reverted; the split code stays as a
  record.

A real backend presents once per engine frame, so this cost is a property of the benchmark harness
rather than of the renderer.

### Not yet matching GL (2026-10-13)

The world, sprites, models, the 2D layer, wipes, fixed colormaps (invulnerability, light amplification and
torches, `fixedcolormap` and `lightamp`), mirrors, stacked sectors, skyboxes and horizons are ported, each
with a GL-against-Vulkan scenario in `tools/fuactl/scenarios`. Still missing or different:

- a GLDEFS hardware shader's own `ProcessLight`, or one leaning on more of GL's `main.fp` than `getTexel`, `vTexCoord` and `timer`, which draws without it (see `features/hwshaders`);
- nested line-mirror reflections past the first show only the sprites the player's walk through a mirror took, where GL walks every reflection;
- Linux and macOS are built by CI and not yet run here: the window code (`dgsdl.cpp`, `dgcocoa.mm`) and the
  .app's MoltenVK bundle want a first launch on each before they are trusted. Beside GL there they open a
  window of their own, since neither nests one window in another.

## Earlier: untextured geometry, benchmarked

Sunder MAP10's wall geometry renders through Diligent, from data produced by `features/levelmesh`:

```
uploaded 24558 vertices (0.47 MB) from the level mesh; camera snapshotted
Diligent: 500 frames, 24558 verts (8186 tris) -- presented 0.186 ms/frame (5376 fps),
                                                 submit-only 0.006 ms/draw
```

Visually confirmed against the GL window side by side: same canyon, same structure, same viewpoint.
Untextured and shaded by height — materials are a later milestone.

### The benchmark that matters

`fua_diligent_bench` and its GL half, `fua_gl_meshbench` (deleted with GL's mesh path in P3), submitted
**the same baked vertices, the same number of times, each GPU-synced before the clock stops.** Nothing else about a Diligent frame and an engine
frame is comparable — textures, lighting, sprites, the BSP walk — so only this is a fair number.

| backend | submit-only, 8186 tris |
|---|---|
| OpenGL | 0.0080 / 0.0040 ms |
| Diligent (Vulkan) | 0.0080 / 0.0060 ms |

**They are the same, to the limit of the timer.** Which is the expected answer once stated plainly:
both issue one draw call for 8k triangles to the same GPU, and the API is not what costs. Diligent's
presented figure (~0.19 ms) is swapchain present, not drawing.

**So the swap is confirmed, by measurement, not to be a performance play.** It buys portability
(Metal via MoltenVK, where GL is capped at 4.1 with no compute) and a path to ray tracing. The
frametime that remains in the real renderer is CPU visibility work — the BSP walk and clipper, ~0.85
ms — which no backend API touches and only GPU culling removes.

## Milestone 1 — device creation, verified

`fua_diligent_probe` creates a Vulkan device through Diligent inside the running engine and reports
the adapter. Measured on the dev machine:

```
Diligent Vulkan device created
  adapter: NVIDIA GeForce RTX 5080 (discrete)
  API version: 1.4
```

The GL renderer keeps rendering afterwards — the two APIs share nothing but the process until a
swapchain is involved, and that is confirmed rather than assumed.

Nothing else exists yet: no swapchain, no window, no drawing.

## What milestone 1 was for

Integration friction, found early and cheaply rather than late. Three real problems surfaced, all now
fixed in the build:

1. **C++17.** Diligent's `RefCountedObjectImpl.hpp` uses `std::align_val_t` (aligned new). The engine
   compiles at C++14, and raising the whole tree for one dependency is how something unrelated breaks
   three files away — so the bump is confined to `dgprobe.cpp` via `set_source_files_properties`.
2. **CRT mismatch.** The engine links the static CRT (`/MT`); Diligent defaults to dynamic, giving
   `LNK2038` on every Diligent object. The same failure `tests/CMakeLists.txt` documents for
   GoogleTest.
3. **`CMAKE_MSVC_RUNTIME_LIBRARY` does not work here** — it needs `CMP0091 NEW`, and Diligent's own
   `cmake_minimum_required` resets the policy before its targets are defined. The pre-CMP0091 idiom
   (rewriting `/MD` to `/MT` in the inherited flag variables, before `add_subdirectory`) does work.

## What this does NOT buy

Worth stating, because it is the opposite of the intuition: **the swap is not a frametime win.**
Measurement (`docs/levelmesh-PLAN.md`) puts the remaining CPU in the visibility decision — the BSP
walk and the clipper, ~0.85 ms — and per-draw state. A different backend API touches neither. What
the swap buys is portability (Metal via MoltenVK, where OpenGL is capped at 4.1 and has no compute)
and a path to ray tracing.

The frametime win lives in GPU culling + indirect draws, which is reachable in GL 4.3 today without
swapping anything.

## Next

| milestone | state |
|---|---|
| 1 device creation | done — Vulkan 1.4 on RTX 5080, in-process, beside live GL |
| 2 swapchain + present | done — 300 frames |
| 3 pipeline + shaders | done — GLSL to SPIR-V via `GLSL_VERBATIM` |
| 4 backend seam | skipped for now; went straight to geometry |
| 5 scene geometry | **done — Sunder MAP10 renders, benchmarked** |
| 6 textures | done — materials, translations, warp and brightmap shaders |
| 7 lighting, sprites, flats | done — see "Not yet matching GL" for what is left |
| 8 visibility | the walk runs on the job pool; GPU culling with indirect draws is not started |

### Console commands

| command | what |
|---|---|
| `fua_diligent_probe` | create the Vulkan device, report the adapter |
| `fua_diligent_window` | swapchain + triangle pipeline on a separate window |
| `fua_diligent_frame N` | present N triangle frames |
| `fua_diligent_scene` | upload the baked level mesh + snapshot the camera |
| `fua_diligent_bench N` | presented and submit-only timings |
| `fua_diligent_shot [file]` | read the swapchain back to a PNG -- the only trustworthy visual check |

Driven in one step by **`fuactl diligent --port P [--frames N] [--shot FILE]`**, which bakes the
mesh, uploads, screenshots and runs the benchmark in the order they actually require.

Requires `gl_wallmesh 1` and a walked level, so the mesh has baked geometry to hand over.

## Other backends: Metal and D3D12

**Metal** is not in DiligentCore: `Graphics/GraphicsEngineMetal` holds only its interface headers, and
Diligent's README offers the backend to commercial clients. macOS therefore draws through Vulkan on
MoltenVK, which the .app bundles (`mac_compile.sh`, `dgcocoa.mm`).

**D3D12** is in DiligentCore, open source, and switched off here (`DILIGENT_NO_DIRECT3D12`). It is not
free, because of the shaders. Every one of the backend's shaders is GLSL compiled verbatim (37 sources:
36 at `#version 450`, the traced mirrors' ray-query shader at 460, the
scene shader alone 570 lines in `dgscene_glsl.h`), and Diligent compiles GLSL only for Vulkan and GL; its
D3D12 backend takes HLSL or DXIL. A mod's GLDEFS hardware shaders are GLSL too. So D3D12 needs one of:

- an HLSL twin of every shader, kept in step with the GLSL by hand; or
- a cross-compile step, GLSL to SPIR-V (glslang, already linked) to HLSL (SPIRV-Cross, already linked) to
  DXIL (DXC). Less code, but each shader then needs its bindings mapped to registers and spaces, its
  combined image samplers split, `gl_VertexIndex` checked against `SV_VertexID` (which leaves out the base
  vertex), and the ray-query shader taken to shader model 6.5, with what SPIRV-Cross cannot translate
  written by hand.

Either way every scenario would need a D3D12 side, since the harness compares GL against Vulkan only. What
it would buy is Windows drivers whose Vulkan is poor, which Vulkan on Windows already covers otherwise.
It stays off; if it is wanted, the cross-compile step is the way in.

## Repo note

`deps/DiligentCore` is a git submodule, its revision pinned by the superproject. A clone needs
`git submodule update --init --recursive` (DiligentCore brings glslang, SPIRV-Tools, SPIRV-Cross and volk
as its own submodules); CMake stops with that instruction when they are missing, and
`-DFUA_DILIGENT=OFF` builds GL (Legacy) alone.
