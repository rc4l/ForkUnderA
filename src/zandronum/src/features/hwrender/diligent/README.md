# features/hwrender/diligent — the renderer backend swap

Vendored backend: **DiligentCore** (`deps/DiligentCore`, Apache-2.0, rev `bb821b7`), Vulkan only.
Apache-2.0 is GPL-3 compatible, so this does not endanger the fork's licensing position.

**Off by default.** Configure with `-DFUA_DILIGENT=ON`; a normal build compiles none of it, links none
of it, and takes none of its build time.

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

### Not yet ported

Real lighting (`R_DoomLightingEquation`, distance falloff, fog, dynamic lights), texture animation,
translations, warp/brightmap shaders, flats, sprites, and the 2D/HUD layer. Light is currently a flat
per-piece multiply.

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

`fua_gl_meshbench` and `fua_diligent_bench` submit **the same baked vertices, the same number of
times, each GPU-synced before the clock stops.** Nothing else about a Diligent frame and an engine
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
| 6 textures | next, and large: `FMaterial`, translations, warp/brightmap shaders |
| 7 lighting, sprites, flats | after textures |
| 8 visibility | where the frametime actually is — GPU culling, not the backend |

### Console commands

| command | what |
|---|---|
| `fua_diligent_probe` | create the Vulkan device, report the adapter |
| `fua_diligent_window` | swapchain + triangle pipeline on a separate window |
| `fua_diligent_frame N` | present N triangle frames |
| `fua_diligent_scene` | upload the baked level mesh + snapshot the camera |
| `fua_diligent_bench N` | presented and submit-only timings |
| `fua_gl_meshbench N` | the GL half of the matched comparison |
| `fua_diligent_shot [file]` | read the swapchain back to a PNG -- the only trustworthy visual check |

Driven in one step by **`fuactl diligent --port P [--frames N] [--shot FILE]`**, which bakes the
mesh, uploads, screenshots and runs both halves of the benchmark in the order they actually require.

Requires `gl_wallmesh 1` and a walked level, so the mesh has baked geometry to hand over.

## Repo note

`deps/DiligentCore` is a shallow clone with submodules, 215 MB on disk. `deps/` is already in
`.gitignore`, so nothing of it is tracked — which also means **a fresh checkout has no backend**.
Before this is more than an experiment it needs to become a real submodule (with the rev pinned) or a
documented fetch step, or `-DFUA_DILIGENT=ON` simply fails to configure for anyone else.
