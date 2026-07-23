# Vendored: UZDoom `libraries/ZVulkan`

Self-contained Vulkan support library used by the vendored `rendering/vulkan` backend. It bundles its
own dependencies, so **no system Vulkan SDK is required to build** (see
`docs/hwrender-portability-scope.md` §8):

- `src/volk/` — the Vulkan loader (function pointers resolved at runtime)
- `src/vk_mem_alloc/` — the Vulkan Memory Allocator
- `src/glslang/` — the GLSL → SPIR-V compiler (runtime shader compilation)

## Provenance

- **Upstream:** https://github.com/UZDoom/UZDoom
- **Branch:** `trunk`
- **Commit:** `7bfbf612d9d8197c36bb77ab171005bce521a514` (2026-07-18)
- **Source path:** `libraries/ZVulkan/`  →  here: `src/zandronum/ZVulkan/`

## Build wiring

Gated behind CMake `HAVE_VULKAN`: `add_subdirectory(ZVulkan)` + link `zvulkan`, with the Vulkan
backend sources compiled only when the option is on (a separate source list, exactly as upstream).
The OpenGL/GLES backends do not depend on this library.

## Local modifications

None — pristine vendor. Record any future local edit here with its reason.
