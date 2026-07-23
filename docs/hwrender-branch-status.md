# hwrender port — branch status (`feature/hwrender-port`)

Honest state of the modern GL/Vulkan renderer port on this branch. Companion to
`docs/hwrender-portability-scope.md` (scoping/architecture), `docs/hwrender-integration-plan.md`
(the staged integration), and `docs/hwrender-PLAN.md` (the prior-attempt log this branch mines).
**WIP — do not merge.**

## Where the code came from

The prior attempt (`archive/hwrender-port` in the sibling clone, 79 commits, reached mid-P3 with
MAP01 rendering through a core context) was 117 commits stale and **predates the `fixed_t` 64-bit
widening**. This branch transplanted it onto current `main` via per-file 3-way merges and
reconciled it with the widening and with this branch's own foundations. The verbatim UZDoom vendor
(`rendering/`, `ZVulkan/`, pinned `7bfbf61`) is kept as the re-pull reference; the build compiles the
archive's *adapted* copy at `features/hwrender/backend/`.

## Compiles and passes (verified)

**CI (PR #36, all three platforms): green.** `test` (ASan/UBSan/Leak + coverage gate + clang-tidy),
`build-linux`, `build-windows`, `build-macos` all pass on run 29954079758. Three CI rounds of
cross-platform fixes got there: the SDL 1.2 `USE_XCURSOR` XDisplay hack replaced with SDL2 native
color cursors (Linux), `M_PI` guarded for MSVC, an MSVC-exposed **real ODR violation** fixed by
renaming the legacy class to `LegacyFRenderState` (two unrelated `FRenderState` classes mangled
identically; ELF/Mach-O silently tolerated it), a C2445 conditional ambiguity in `s_sound.h`, and
the C++20-ill-formed template-id destructor in `tarray.h`.

- **Full engine build green** (`mac_compile.sh` exit 0, app + pk3): the ported UZDoom GL backend +
  shims + glue, the 2D drawer, the legacy-scene capture hooks, **native SDL2** (sdl12-compat no
  longer linked; `sdlvideo.cpp`/`SDLMain.m` deleted), and the core-context request path
  (`vid_hwrender` → `zx::ResolveBackend` → `ComputeGLContextRequests`).
- **184/184 tests green**, 100% coverage gate on all `computation/` units (13 units: the archive's
  ten + this branch's 64-bit-safe `vertexconvert`, `glcontext`, `backendselect` — the archive's
  32-bit versions of the latter two were superseded, not merged).
- **`fixed_t` reconciliation:** the strong `zx::Fixed` type caught upstream's stale
  `typedef int32_t fixed_t` in the backend shim at compile time (backend has zero `fixed_t`
  consumers — pure collision, now defers to `basictypes.h`); the `textures.h` conflict was resolved
  keeping the fixed64 casts; every applied hunk audited for `FRACBITS`/narrowing patterns.
- **pk3 verified:** 30 `shaders/hwrender/` lumps + the OpenGL Options menu are in `zandronum.pk3`.
- **Vulkan wiring — verified building and linking:** `HAVE_VULKAN` (default OFF — CI untouched) +
  `add_subdirectory(ZVulkan)` + link; `mac_compile.sh` auto-detects the MoltenVK/vulkan-headers/
  vulkan-loader brew kegs and opts in, or warns and builds without (both paths built green).
  `build/ZVulkan/libzvulkan.a` compiles (volk + vk_mem_alloc + glslang) and links into the app; the
  bundler carries `libvulkan.1.dylib` at `@loader_path`, so the app stays self-contained. MoltenVK
  ICD discovery at runtime is a P5 item. `vid_hwrender 2` resolves to core GL until a device exists.

## NOT verified (the honest gap)

- **Keyboard/mouse input** under native SDL2 (scancode path) needs a human at the keyboard.
- The Vulkan *backend* (`rendering/vulkan`) is not compiled — that is plan P5, behind the same seam;
  ZVulkan-the-library is the adjacency this branch wires.
- Core-path parity: see the three user-confirmed defects below.

## Runtime verification (MCP + user, this session)

- **Legacy path (`vid_hwrender 0`): verified unregressed under native SDL2.** MAP01 renders
  correctly (geometry, weapon, HUD icons, crosshair); "GL context: 2.1 compatibility" logged.
- **Backend selector menu: verified on screen** — "Renderer backend (restart)" renders at the top of
  the existing OpenGL Options menu with the RendererBackends labels. (First attempt collided with
  the pre-existing `OpenGLOptions` in `menudef.z`, which redefines both it and `VideoOptions` and
  overrides `menudef.txt` — the selector now lives in the `.z` menu and the `.txt` duplicates are
  reverted. Note the add_pk3 trap AND the app-bundle pk3 copy both needed manual refresh.)
- **Core path (`vid_hwrender 1`): renders MAP01 end to end** — walls, flats, lighting, weapon, HUD —
  on the 64-bit fixed_t base, with the legacy #version-120 lumps failing exactly as a core context
  must. **Three defects confirmed by the user on the live screen** (user observation is the
  recording standard here; screenshot-reading has been wrong before):
  1. **Fonts deformed** — the 2D hook drops DrawParms render style/translation.
  2. **Weapon sprite cut in half** — the texture-sampling slice (padding vs adopted-handle).
  3. **Weapon sprite blinks bright/dark** — sprite lighting captured as a global at gl_SetColor
     time, inheriting the previous surface's color.
  All three are the plan's known core-path defect classes; the plan's verdict stands — they are
  fixed by adopting the vendored F2DDrawer/sprite path rather than patching the bespoke queue.
- **Verification workflow going forward (user direction): manual end-to-end tests** — the user
  drives the visual verdicts on the live game; automated A/B and screenshots support, not decide.

## Next actions, in order

1. MCP runtime A/B: `vid_hwrender 0` regression check, then `1`, on the fixed scene set.
2. Chase the archive's known core-path defects (sprite slice; 2D `Dim`/`FlatFill` hooks) — now with
   the F2DDrawer vendored and compiling.
3. P4 legacy deletion once parity; P5 Vulkan device bring-up behind the seam.
