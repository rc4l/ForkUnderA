# hwshaders

GLDEFS `HardwareShader` blocks for every renderer. GL's shader manager used to keep the list of
shader lumps itself; it lives here now, so the Vulkan backend can compile the same shaders.

- `hwshaders.{h,cpp}`: the parse (`ParseHardwareShader`, moved from `gl_ParseHardwareShader`) and
  the lump list a texture's `gl_info.shaderindex` points into.
- `computation/hwshader_compute`: a mod's GL fragment code adapted to sit beside the backend's own
  shader (`ComputeAdaptUserShader`), the block of shims and entries the scene shaders take
  (`ComputeUserShaderBlock`), splicing it in (`ComputeSpliceShaderBlock`), the piece's shader
  number in the warp slot (`ComputePieceShader`), and the console note for a shader left out
  (`ComputeUserShaderSkipNote`).

The backend checks each shader compiles on its own before adding it, so one that uses something
GL's `main.fp` has and the backend does not draws plainly rather than breaking the world pipelines.
GL does the same: a GLDEFS shader that does not compile is replaced by the default shader under its
name instead of ending the game at startup, as it used to (DnD's, written against Zandronum 3.3's
`camerapos` and the compatibility profile's `gl_TextureMatrix`, stopped every GL launch). Both print
`<renderer>: hardware shader <lump> does not compile here; its textures draw without it`, GL with the
compiler's errors under it. Scenario `hwshaderbroken`.
A shader's own `ProcessLight` is not called: the backend lights every surface with its own model.

## A snapshot, not an interface that grows

What a shader may use is frozen at what GL's `main.fp` gave it: `ProcessTexel` (or the older
`Process`), `getTexel`, `vTexCoord` and `timer`. Nothing is added to that set; mods written against it
keep working on both renderers, and the whole mechanism is to be deprecated later rather than extended.

## In-engine hooks

- `gl/shaders/gl_shader.cpp`: `gl_ParseHardwareShader` calls `ParseHardwareShader`, and
  `CompileShaders` takes the lumps from `UserShaderCount`/`UserShaderLump`, compiling each through
  `TryCompile` and falling back to the default shader with `ComputeUserShaderSkipNote`'s note.
- `features/materials/materials.cpp`: `MaterialInfo::userShader` from `UserShaderOf`.
- `features/hwrender/diligent`: `dgupload.cpp` puts `ComputePieceShader` in the piece's warp slot,
  `dgpipelines.cpp` builds the block (`UserShaderBlock`) and `CompileShader` splices it into every
  scene shader at `//FUA_USER_SHADERS`.
