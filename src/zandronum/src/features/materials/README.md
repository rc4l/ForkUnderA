# features/materials

Textures for every renderer: a texture's pixels, its layout as a material, and what it is (size, mask,
warp, palette). GL and the Vulkan backend both read them from here; GL keeps only binding them
(`gl/textures/gl_material.cpp`) and its own filtering and formats (`gl/textures/gl_texture.cpp`).

## What lives here

- `material.h/.cpp`: `FMaterial` and `FTexturePixels`, moved out of `gl/textures/gl_material.*` the way
  later GZDoom split them. `FTexturePixels` is the CPU half of GL's old `FGLTexture`: Doom's texture
  composition (multipatch, translations), hires replacements and upsampling. `FMaterial` is the
  layers, the shader index, the size and offsets, and a sprite's frame and trim. A renderer hangs its
  own copy of the pixels off `FTexturePixels::mHwTexture` (`hardwaretexture.h`, `IHardwareTexture`),
  which a flush deletes.
- `computation/materiallayout_compute`: the material's layout (`ComputeMaterialLayout`), a sprite's
  trim (`ComputeTrimBorders`), and the device's texture size limit (`ComputeStoredDimension`).
- `computation/texcoord_compute`: `FTexCoordInfo` and how a wall's texture coordinates scale on a
  material (`ComputeTexCoordInfo`).
- `textureinfo.h/.cpp`: a texture's renderer data (`FTexture::gl_info`): brightmaps, glow and sky colours,
  holes, transparency and edge smoothing, the GLDEFS `brightmap` and `detail` blocks, `gl_flush`,
  `textureinfo` and `gl_texture_usehires`.
- `texturebitmap`, `translationpalette`, `skyboxtexture`, `hqresize`, `hirestex`, `hqnx`, `hqnx_asm`:
  moved from `gl/textures` and `gl/` unchanged but for their names (`FTextureBitmap` was
  `FGLBitmap`, `FTranslationPalette` was `GLTranslationPalette`).
- `materials.h/.cpp`: what the backend asks of a material, through an opaque `MaterialRef` (an
  `FMaterial` address, stable for the level and compared by identity): its identity, the frame an
  animated one shows, its brightmap, its pixels under a translation, its sizes (`MaterialRenderSize`,
  `MaterialStoredSize`), whether it has see-through texels (`MaterialTransparent`), and how a wall's
  texture coordinates scale on it (`MaterialTexCoordInfo`).

The texture system keeps its own limits (`SetTextureLimits`: whether a side may be any size, and the
largest), which GL sets from its context and a launch without GL from the backend's
(`AssumeBackendTextures`). Such a launch also sets the texture system up through `GenerateGlobalBrightmap`
and `CreateTranslationPalette`, and the backend's sky reads a GLDEFS sky box's faces from it (`SkyBoxFacesOf`).

`MaterialIfMade` answers without making a material, so a worker thread may ask; the sprite batch hands a
sprite whose material is not made yet back to the game thread.

It also keeps glstuff's round and smooth particle pictures and the mirror sheen
(`ParticleTexture`, `MirrorSheenTexture`), which GL created for itself at startup.

## In-engine hooks

- `gl/textures/gl_material.h/.cpp`: includes `material.h`; `gl_BindMaterial`, `gl_PrecacheMaterial`,
  `gl_BindMaterialToFrameBuffer` and `gl_ClearLastMaterial` replace `FMaterial::Bind`, `Precache`,
  `BindToFrameBuffer` and `ClearLastTexture`, called from `gl_renderstate.h` (`SetMaterial`),
  `gl_texture.cpp` (`PrecacheGL`), `gl_scene.cpp`, `gl_wipe.cpp` and `gl_hwtexture.cpp`.
- `gl/textures/gl_hwtexture.h`: `FHardwareTexture` implements `IHardwareTexture`.
- `gl/system/gl_interface.cpp` `gl_LoadExtensions`: `SetTextureLimits` from GL's context.
- `gl/renderer/gl_renderer.cpp` `FGLRenderer::Initialize`: `glpart`, `glpart2` and `mirrortexture` are
  taken from here, and no longer freed by GL.
- `textures/textures.h`: `gl_info.SystemTexture` holds `FTexturePixels`.
- `win32/win32gliface.cpp`, `posix/sdl/sdlglvideo.cpp`: `FMaterial::FlushAll` on a mode change.

`materials.cpp` reads `FMaterial`, `FTexture`, `TexMan` and `FTranslationPalette`, and nothing in `gl/`.
