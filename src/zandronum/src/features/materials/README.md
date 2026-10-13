# features/materials

Materials for a renderer that must not know GL's `FMaterial`: a texture's identity, the frame an
animated one shows, its brightmap, its pixels under a translation, and what it is (size, mask,
warp, palette). The Vulkan backend reaches textures only through `materials.h`.

A `MaterialRef` is an `FMaterial` address, stable for the level and compared by identity; only
`materials.cpp` looks inside it. Pixels still come from `FMaterial::CreateTexBuffer`, which is where
Doom's texture composition (multipatch, translations, hires replacements) lives.

The other modules ask it what they used to ask `FMaterial` directly: the material a wall or flat's texture
id is drawn with (`MaterialForTextureId`), its texture (`MaterialTexture`), the size GL draws it at and the
size it is stored at (`MaterialRenderSize`, `MaterialStoredSize`), whether it has see-through texels
(`MaterialTransparent`), and how a wall's texture coordinates scale on it (`MaterialTexCoordInfo`).
A launch with no GL context sets the texture system up through it too (`AssumeBackendTextures`,
`GenerateGlobalBrightmap`, `CreateTranslationPalette`), and the backend's sky reads a GLDEFS sky box's
faces from it (`SkyBoxFacesOf`). `computation/texcoord_compute` is `FTexCoordInfo`, those coordinates' offset and width rules, moved from
`gl/textures/gl_material.h`, which includes it.

`MaterialIfMade` answers without making a material, so a worker thread may ask; the sprite batch hands a
sprite whose material is not made yet back to the game thread.

It also keeps glstuff's round and smooth particle pictures and the mirror sheen
(`ParticleTexture`, `MirrorSheenTexture`), which GL created for itself at startup.

## In-engine hooks

- `gl/renderer/gl_renderer.cpp` `FGLRenderer::Initialize`: `glpart`, `glpart2` and `mirrortexture` are
  taken from here, and no longer freed by GL.
- `gl/textures/gl_material.h`: includes `computation/texcoord_compute.h` for `FTexCoordInfo`.

`materials.cpp` reads `FMaterial`, `FTexture`, `TexMan` and `GLTranslationPalette`.
