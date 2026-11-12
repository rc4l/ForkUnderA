# hwshaders

GLDEFS `HardwareShader` and `Material` blocks for every renderer. GL's shader manager used to keep the
list of shader lumps itself; it lives here now, so the Vulkan backend can compile the same shaders.
Modder reference: [`docs/modding/gldefs/Texture_shaders.md`](../../../../../docs/modding/gldefs/Texture_shaders.md).

- `hwshaders.{h,cpp}`: the parse (`ParseHardwareShader`, moved from `gl_ParseHardwareShader`, and
  `ParseMaterial`, both through `ParseShaderBlock`), the shader list a texture's `gl_info.shaderindex`
  points into, one per key (`ComputeUserShaderSlot`), each texture's layers (`LayerTexture`) and
  specular settings (`SpecularWord`), and the shader a texture draws with (`TextureUserShader`).
- `computation/hwshader_compute`:
  - Zandronum 3.3's names made to work on both renderers (`ComputeLegacyShaderNames`).
  - A shader's entries and how it is called: `ComputeShaderEntries`, `ComputeProgramEntry` (UZDoom's
    order: `SetupMaterial`, `ProcessMaterial`, `GetTexCoord`, `ProcessTexel`, `Process`), and
    `ComputeLightHook` for either `ProcessLight`.
  - A block's key (`UserShaderKey`): lump, `fua_ProcessLighting`, defines, `Texture` names, material type
    and uniform set; GL's refusal of what only Vulkan runs (`ComputeGLRefusal`).
  - UZDoom's layers and samplers per type (`ComputeNumberedSampler`, `ComputeCustomTextureLimit`), the
    type a texture's maps give (`ComputeMaterialTypeFromLayers`), and the runtime check of a shader against
    them (`ComputeMaterialShader`), with a built-in shader per type (`ComputeBuiltinMaterialKey`,
    `ComputeBuiltinMaterialSource`, UZDoom's `func_spec.fp` and `func_pbr.fp`).
  - The block one shader's program ends in (`ComputeUserProgramBlock`, `ComputePlainProgramBlock` for
    none), a mod's code adapted for it (`ComputeAdaptUserShader`), and the console note for a shader left
    out (`ComputeUserShaderSkipNote`).

A `HardwareShader PostProcess` block is not a texture's: `ParseHardwareShader` hands it to
`features/postprocess`.

A shader that does not compile draws its textures plainly on both renderers instead of ending the game
at startup, as GL used to. Both print `<renderer>: hardware shader <lump> does not compile here; its
textures draw without it`, GL with the compiler's errors under it, Vulkan after Diligent's. Scenario
`hwshaderbroken`.

## Materials and light models

UZDoom's `Material` struct is filled as its `main.fp` fills it (`SetMaterialProps`, `ApplyNormalMap`
with the normal map's derivative frame), and `fuaShadeMaterial` (`dgscene_glsl.h`) lights it as
`getLightColor` does: sector light and glow, the material's glow and bright, the shader's `ProcessLight`,
then dynamic lights through the type's model (`fuaSpecularLit`, Blinn-Phong; `fuaPBRLit`,
Cook-Torrance), both over `FUA_LIGHT_LOOP_GLSL`, the plain path's light loop. Layers reach a program
through the material array: a slot's row of 16 layer slots (`ComputeLayerRow`) after the slot remap,
which the vertex stage hands on as `vLayers0/1`; without the array a texture keeps the plain light.
Scenarios `hwshaderparity` and `hwmaterials`, each row checked against UZDoom's Vulkan backend (2D quads
read only custom layers, `uExtra0..7`).

## One program per shader

On Vulkan each GLDEFS shader is a program of its own, as on GL: the scene fragment stage with that one
shader's block at its end, after `main`, so nothing the shader defines reaches the backend's code or
another shader. Two shaders defining one macro or helper differently both run (DnD's `EDGE_POW` and
`h1` broke the old single block, and with it every shader). Every shader's programs are compiled at launch,
once GLDEFS is read, as GL compiles its own at startup (`PrecompileUserShaders`, from the backend renderer's
`Init`, or with the first level when the device came up later; `fua_dg_precompile_shaders 0` leaves them to
their first draw), and a shader that does not compile is noted then. A shader's pipelines are made on the
device's pool: the first time a piece asks for one (`ScenePSO`, `Pso2D`), which draws as the plain pipeline
until the driver has it rather than holding the frame, the level's own surfaces at upload
(`PrewarmUserPipelines`, all started before any is waited for), and every pair asked for again whenever the
world pipelines are made (`fua_dg_async_pipelines 0` waits for each in turn, as a device that cannot wait for the GPU always does, a browser's among them: `computation/asyncpipelines_compute`). Each takes the same resources as the plain
pipeline and commits its bindings (`IsCompatibleWith`), so the bindless material array is not repeated per
shader; one that does not fit them is left out with the note above. Compiled shaders are kept by their
text for the session (`gpu/made/shader_hits`), so a level change compiles none it has compiled before.

A HUD image runs its texture's shader too, as Zandronum 3.3's `BindPatch` ran it (DnD's mana bar is one):
GL's `DrawTexture` asks for the material's shader (`ComputeDrawTextureShader`), and the backend's 2D layer
has programs of its own, the same block for `k2DProgram` at `//FUA_2D_SHADERS` (`dg2d.cpp`'s `Pso2D`).

## How a shader is called

`ComputeShaderEntries` finds a shader's entries as UZDoom's `gl_shader.cpp` does, and
`ComputeProgramEntry` turns them and the block's `fua_ProcessLighting` into one entry, in UZDoom's order:

| Entry | Shader | What it is handed | After it |
|---|---|---|---|
| `kEntrySetupMaterial` | `SetupMaterial(inout Material)` (UZDoom) | a zeroed material | lit through `fuaShadeMaterial` |
| `kEntryProcessMaterial` | `Material ProcessMaterial()` (GZDoom 3.x) | nothing | lit without glow or bright, as UZDoom's `LEGACY_USER_SHADER` |
| `kEntryTexCoord` | `vec2 GetTexCoord()` (UZDoom) | nothing | `SetMaterialProps` at it, then lit |
| `kEntryTexel` | `ProcessTexel()` (GZDoom, UZDoom) | nothing | lit, multiplied by the draw's colour and alpha |
| `kEntryProcessLit` | `Process(color)`, `fua_ProcessLighting uzdoom` | white, as UZDoom's `func_defaultmat.fp` | lit like a texel |
| `kEntryProcessFinal` | `Process(color)`, the default | the light (`getLightColor` with modulated dynamic lights, no brightmap) and the draw's alpha, as Zandronum 3.3's `main.fp` | only coloured fog, or a fixed colormap's recolouring of what it returned (handed white then, as 3.3's `main_colormap.fp`) |

A `ProcessLight(color)` or `ProcessLight(Material, color)` is the program's light hook
(`ComputeLightHook`), called with the light before the dynamic lights, as both UZDoom and 3.3 call it.

So a 3.3 shader's glow, added without `color`, shows in the dark as on 3.3 (DnD's Volcano and Doomguard
lava; scenario `hwshaderlighting`, with an oracle map per case). A `Process` shader keeps the alpha it
returns; DnD packs a state into a HUD message's alpha this way. On a HUD image the draw's colour is the
light: `kEntryProcessFinal` hands it, `kEntryProcessLit` multiplies it after.

GL picks the entry with `ComputeGLEntryDefine` (`FUA_PROCESS_ZANDRONUM` or `FUA_PROCESS_UZDOOM` ahead of
`main.fp`). The backend's scene stages are split as `main.fp`'s are (`fuaLitAt`, `fuaClipSurface`,
`fuaLightUp`, then `fuaColouredFog` or `fuaFixedTint`), and each entry's row in `ComputeUserProgramBlock`
defines `fuaSurfaceTexel`, `fuaSurfaceAlpha` and `fuaSurfaceShade`; `kEntryProcessFinal` calls
`fuaProcessLight` to hand `Process` the light and `fuaShadeFinal` to finish. The 2D entry is `fua2DColor`.

## What a shader can read

GL's `main.fp` names, on Vulkan under the scene shader's own (scenario `hwshadernames`):

| Name | Vulkan reads |
|---|---|
| `pixelpos` | `vPixelPos`: world position in GL's axis order (x, height, y), w the eye depth |
| `uCameraPos` | the eye in the same order, w 0 |
| `vColor` | the surface's colour, alpha its alpha |
| `glowdist`, `uDesaturationFactor` | `vGlowDist`, `vDesaturation` |
| `ModelMatrix` | a model's model-to-world matrix, carried per frame in the dynamic words (`DynAddMatrix`); identity elsewhere |
| `TextureMatrix` | identity, since the backend's texture coordinates are final |
| `vTexCoord`, `timer`, `getTexel`, `texture2D(tex, uv)`, `texture(tex, uv)` | the piece's coordinate, its time at its speed, its texel |
| `vWorldNormal`, `uSpecularMaterial` | the surface's world normal in the same order, w 0; the texture's glossiness and specular level |
| `textureSize`, `texelFetch` on any sampler name | through `fuaLayerSize` and `fuaLayerFetch`, 1x1 and black for a layer the texture lacks |

Zandronum 3.3's names work on both renderers: `camerapos` reads `uCameraPos.xyz` unless the shader
defines or declares its own, `gl_TextureMatrix[i]` reads `fuaTextureMatrix(i)` (7 `ModelMatrix`, 0
`TextureMatrix`, else identity), loose `uniform vec3 camerapos;`, `varying vec4 pixelpos;` and `in vec4
pixelpos;` lines are dropped, and `gl_TexCoord[0]` reads `vTexCoord`. Not on Vulkan: `ViewMatrix`,
`ProjectionMatrix`, `uObjectColor` and GL's other render-state uniforms.

## Uniforms scripts set

A block's `fua_Name "<set>"` and `fua_Uniform <type> <name> [= <values> | = cvar <cvar>]` lines declare
uniforms that `fua_SetShaderUniform*` (and the console's `shaderuniform`) set by the set's name, as they
set a post-process shader's. Every block naming one set shares its uniforms.
- `features/postprocess` keeps the sets beside the post-process declarations (`ParseTextureShaderUniform`,
  `TextureShaderSet`), so the level's state table, the ACS calls, the netcode, savegames and a dedicated
  server's reading of GLDEFS (`ComputePostProcessDeclarations` keeps a texture block's `fua_` lines) all
  serve them unchanged. `fua_SetShaderEnabled` refuses a set's name, as a set has nothing to turn on.
- Each uniform takes one of the `kTextureUniformSlots` (64) vec4s every texture shader shares. They sit
  after the scene's constant block (`FUA_SCENE_CONSTANTS_GLSL`) and the 2D layer's (`FUA_SCREEN2D_GLSL`),
  written each pass when any set is declared (`WriteTextureUniforms`, `TextureUniformBlock`).
- A program names each uniform as its vec4 (`ComputeUniformLines`, an int read from its bits) and drops
  the shader's own `uniform <type> <name>;` line (`ComputeDropUniformDeclarations`).
- GL, an internal fixture, refuses such a shader (`ComputeGLRefusal`).

Scenarios `hwshaderuniforms` and `hwshaderuniformsopt` (the SPIR-V optimizer on), and a wall set by a
server script in `netshaders`.

## In-engine hooks

- `gl/shaders/gl_shader.cpp`: `gl_ParseHardwareShader` calls `ParseHardwareShader`; a shader's source
  goes through `ComputeLegacyShaderNames`; `CompileShaders` takes the lumps from
  `UserShaderCount`/`UserShaderLump`/`UserShaderLighting`, compiling each through `TryCompile` and falling
  back to the default shader with `ComputeUserShaderSkipNote`'s note, a built-in material shader to the
  default quietly; `FShader::Load` refuses what only Vulkan runs (`ComputeGLRefusal`) and puts
  `ComputeGLEntryDefine`'s line ahead of `wadsrc/static/shaders/glsl/main.fp`, which calls `Process` as
  that define says.
- `features/gldefs/gldefs.cpp` `gl_ParseDefs`: the `material` tag calls `ParseMaterial`.
- `gl/renderer/gl_renderer.cpp`: `DrawTexture` takes `ComputeDrawTextureShader`'s shader.
- `features/materials/materials.cpp`: `MaterialInfo::userShader` and `MaterialShading` from
  `TextureUserShader`; `MaterialLayer`, `MaterialHasLayers` and `MaterialSpecularWord` for the layer rows.
- `features/levelmesh/modelmesh.cpp`: a captured model's matrix added with its first surface
  (`DynAddMatrix`, `MeshPiece::modelMatrix`).
- `features/frame/backendrenderer.cpp`: `Init` calls `PrecompileUserShaders` after GLDEFS is read.
- `features/hwrender/diligent`: `dgupload.cpp` puts `ComputePieceShader` in the piece's warp slot and a
  batch's shader in `SceneBatch::userShader`; `dgdynamic.cpp` the same per `DynRun`, and the frame's model
  matrices after its sprites; `dgpipelines.cpp` makes the programs (`ProgramBlock` with the shader's entry,
  `ScenePSO`, `PrewarmUserPipelines`) and leaves a refused style out; `dgscene_glsl.h`'s fragment stages
  shade through `fuaSurfaceShade`; `CompileShader` (`dgscene.cpp`) splices the block at `//FUA_USER_SHADERS` and
  caches what it compiles; the world, solid-model, mirror-decal and translucent loops bind `ScenePSO`;
  `dg2d.cpp` binds `Pso2D` per quad, with the shader's speed on its vertices.
