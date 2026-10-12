# features/materials

Materials for a renderer that must not know GL's `FMaterial`: a texture's identity, the frame an
animated one shows, its brightmap, its pixels under a translation, and what it is (size, mask,
warp, palette). The Vulkan backend reaches textures only through `materials.h`.

A `MaterialRef` is an `FMaterial` address, stable for the level and compared by identity; only
`materials.cpp` looks inside it. Pixels still come from `FMaterial::CreateTexBuffer`, which is where
Doom's texture composition (multipatch, translations, hires replacements) lives.

## In-engine hooks

None. `materials.cpp` reads `FMaterial`, `FTexture`, `TexMan` and `GLTranslationPalette`.
