# hwshaders

GLDEFS `HardwareShader` blocks for every renderer. GL's shader manager used to keep the list of
shader lumps itself; it lives here now, so the Vulkan backend can compile the same shaders.

- `hwshaders.{h,cpp}`: the parse (`ParseHardwareShader`, moved from `gl_ParseHardwareShader`) and
  the shader list a texture's `gl_info.shaderindex` points into, one per lump and `fua_ProcessLighting`
  (`ComputeUserShaderSlot`, `UserShaderLighting`).
- `computation/hwshader_compute`: Zandronum 3.3's names made to work on both renderers
  (`ComputeLegacyShaderNames`, with `ComputeRewriteIndexedName`), a shader's style and how its entry is
  called (`ComputeShaderStyle`, `ComputeStyleRefusal`, `ComputeProcessLightingOf`, `ComputeProgramEntry`,
  `ComputeGLEntryDefine`), a mod's GL fragment code adapted for the backend (`ComputeAdaptUserShader`),
  the block one shader's program ends in
  (`ComputeUserProgramBlock`, `ComputePlainProgramBlock` for none) and splicing it in
  (`ComputeSpliceShaderBlock`), the piece's shader number in the warp slot and back
  (`ComputePieceShader`, `ComputeUserShaderOf`), and the console note for a shader left out
  (`ComputeUserShaderSkipNote`, its reasons indented by `ComputeIndentedLines`).

A `HardwareShader PostProcess` block is not a texture's: `ParseHardwareShader` hands it to
`features/postprocess`.

A shader that does not compile draws its textures plainly on both renderers instead of ending the game
at startup, as GL used to. Both print `<renderer>: hardware shader <lump> does not compile here; its
textures draw without it`, GL with the compiler's errors under it, Vulkan after Diligent's. Scenario
`hwshaderbroken`. A shader's own `ProcessLight` is not called: the backend lights every surface with its
own model.

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
Since a program is made only when something draws with it, `fua_dg_hwshaders` makes every shader's and
lists which run.

A HUD image runs its texture's shader too, as Zandronum 3.3's `BindPatch` ran it (DnD's mana bar is one):
GL's `DrawTexture` asks for the material's shader (`ComputeDrawTextureShader`), and the backend's 2D layer
has programs of its own, the same block for `k2DProgram` at `//FUA_2D_SHADERS` (`dg2d.cpp`'s `Pso2D`).

## How a shader is called

`ComputeShaderStyle` tells a shader's style apart as UZDoom's `gl_shader.cpp` does: `SetupMaterial` or
`ProcessMaterial` first (UZDoom's materials, refused on both renderers with `ComputeStyleRefusal`'s reason
until they are ported), then `ProcessTexel`, else an old `Process(color)`. `ComputeProgramEntry` turns the
style and the block's `fua_ProcessLighting` into one of three entries:

| Entry | Shader | What it is handed | After it |
|---|---|---|---|
| `kEntryTexel` | `ProcessTexel()` (GZDoom, UZDoom) | nothing | lit, multiplied by the draw's colour and alpha |
| `kEntryProcessLit` | `Process(color)`, `fua_ProcessLighting uzdoom` | white, as UZDoom's `func_defaultmat.fp` | lit like a texel |
| `kEntryProcessFinal` | `Process(color)`, the default | the light (`getLightColor` with modulated dynamic lights, no brightmap) and the draw's alpha, as Zandronum 3.3's `main.fp` | only coloured fog, or a fixed colormap's recolouring of what it returned (handed white then, as 3.3's `main_colormap.fp`) |

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

Zandronum 3.3's names work on both renderers: `camerapos` reads `uCameraPos.xyz` unless the shader
defines or declares its own, `gl_TextureMatrix[i]` reads `fuaTextureMatrix(i)` (7 `ModelMatrix`, 0
`TextureMatrix`, else identity), loose `uniform vec3 camerapos;`, `varying vec4 pixelpos;` and `in vec4
pixelpos;` lines are dropped, and `gl_TexCoord[0]` reads `vTexCoord`. Not on Vulkan: `ViewMatrix`,
`ProjectionMatrix`, `uObjectColor` and GL's other render-state uniforms.

## In-engine hooks

- `gl/shaders/gl_shader.cpp`: `gl_ParseHardwareShader` calls `ParseHardwareShader`; a shader's source
  goes through `ComputeLegacyShaderNames`; `CompileShaders` takes the lumps from
  `UserShaderCount`/`UserShaderLump`/`UserShaderLighting`, compiling each through `TryCompile` and falling
  back to the default shader with `ComputeUserShaderSkipNote`'s note; `FShader::Load` refuses a material
  shader (`ComputeStyleRefusal`) and puts `ComputeGLEntryDefine`'s line ahead of
  `wadsrc/static/shaders/glsl/main.fp`, which calls `Process` as that define says.
- `gl/renderer/gl_renderer.cpp`: `DrawTexture` takes `ComputeDrawTextureShader`'s shader.
- `features/materials/materials.cpp`: `MaterialInfo::userShader` from `UserShaderOf`.
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
