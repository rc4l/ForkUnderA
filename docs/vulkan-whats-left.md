# What is left for the Vulkan backend

Written 2026-10-14, after the second parity program (`git log f9d26404..`): every scenario in
`tools/fuactl/scenarios` passes GL against Vulkan on Windows. This is everything known not to be done.

## Still different from GL

- **Camera texture offset.** In `skyviews` MAP02 the low room's camera picture sits a few pixels
  higher or lower on Vulkan than on GL. `cameratex` passes, so it is specific to that framing.
- **Hardware shaders.** A GLDEFS shader's own `ProcessLight` is not called, and one using more of
  GL's `main.fp` than `ProcessTexel`/`Process`, `getTexel`, `vTexCoord` and `timer` draws plainly.
  The interface is frozen by decision (`features/hwshaders/README.md`).
- **One unexplained sprite.** `fua_sprite_check` on `mirrornested` reports a lamp column behind the
  player that only GL's walk drew. It is off screen and changes no picture, and it is not explained.

## Not run yet

- **Linux and macOS** are compiled by CI only, which runs on pull requests and pushes to `main`. The
  window code (`dgsdl.cpp`, `dgcocoa.mm`), the app icon paths and the macOS bundle's MoltenVK (its
  loader found by bare name) want a first launch on each.

## Ray-traced mirrors (`fua_dg_rtmirrors`, off by default)

Not a GL gap: GL has none, and the default planar mirrors match GL. A traced reflection shows the
level, the opaque sprites and the sky, lit and fogged as GL's reflected view is. Left out, smallest
first:

- brightmaps (the piece's brightmap slot is there to sample) and animated textures (the mirror
  pipeline does not bind SlotRemap);
- dynamic lights (the light grid is binned for the screen, so a traced hit would test every light);
- translucent sprites and translucent level surfaces (the level structure is built opaque; a ray
  has to carry on past each translucent layer);
- models (a triangle structure rebuilt every frame from the models' vertices).

## Performance (`docs/hwrender-performance-scope.md`)

- The walk at Sunder MAP14 q43 is 63% of the game thread: the clip 18%, the serial rank replay 12%,
  the wait on the slowest slice 10%.
- The sprite build on wdi07 is 0.48 ms for 1,354 sprites.
- GPU culling with indirect draws waits until the GPU costs something (0.1-0.4 ms everywhere today).

Measure a view with `fuactl costs`, compare settings with `fuactl abperf`, attribute with
`fuactl sample --port P --stacks`.

## Before GL can be removed

- **A fallback:** GL is what runs when no Vulkan device can be made (old GPUs, VMs, some Macs).
- **A reference for the suite:** every scenario judges Vulkan against GL; without GL each needs a
  stored picture or numeric checks.
- **GL's own code in shared places:** `features/damage-tint`, `features/hitboxviz`,
  `features/fua-caching/fua_warmup.cpp`, `features/frame/levelrenderer.cpp`, `features/sidebyside`,
  `features/hwrender/backendvideo.cpp`, `mcp_glperf.cpp`, `mcp_renderinfo.cpp` and the platform GL
  video glue (`win32/win32gliface.*`, `posix/sdl/sdlglvideo.*`).

## Skipped by decision

- D3D12: Diligent's D3D12 backend takes HLSL only, so every shader would be ported.
- The `r_` config back-compat item of the second program.
