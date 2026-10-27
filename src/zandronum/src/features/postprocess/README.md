# features/postprocess

GLDEFS post-process shaders, ported from UZDoom (`uzdoom@62afe5f0a914610bd70ea9302da992ede38a36ad`):
full-screen fragment shaders a mod runs over the 3D scene or over the whole screen. They run on Vulkan
and WebGPU only. GL Legacy draws without them and says so once at launch.

```
HardwareShader PostProcess <beforebloom|scene|screen>
{
	Name "<name>"
	Shader "<lump>" <330..450>
	Uniform <int|float|vec2|vec3|vec4> <name> [= <values> | = cvar <cvar>]
	Texture <name> "<texture>"
	Enabled
}
```

## What a shader reads

A mod's shader is written for GL and compiled after a prolog that gives it GL's names
(`computation/postprocess_compute`'s `ComputePostProcessProlog`):

- `TexCoord`, with y up as on GL.
- `InputTexture` (the picture so far) and `LastInputTexture` (the previous frame's scene).
- Its uniforms, by name.
- Its `Texture` pictures, by name. A `Texture` line's picture reads as it does on GL, its first row at
  `TexCoord` y 0, the bottom.
- UZDoom's `InputTime`, `InputTimeDelta` and `InputTimeGame`, and `fua_EnabledTime`: game seconds since
  the shader went on.

It writes `FragColor`. `gl_FragCoord` and `textureSize(InputTexture, 0)` count the input's pixels with y
up, whatever size the pass draws at.

## When it runs

UZDoom's order, without bloom:

1. The `beforebloom` stage.
2. The view's own 2D: the weapon, the targeter and the screen's tint (`draw2d`'s `ViewQuads`).
3. The `scene` stage.
4. The HUD, the menus and the console.
5. The `screen` stage, over everything.

With no bloom, a `beforebloom` shader differs from a `scene` one only by running under the weapon.

## State

- GLDEFS `Enabled` is how each level starts.
- `shaderenable` and `shaderuniform` change the level's state (`FLevelLocals::fua_postShaders`), which lasts
  until the level ends. A player's own entry wins over everyone's. A timed entry that runs out leaves
  everyone's in place.
- The renderer reads the level's state through `features/gamecopy`, which copies it only when its
  generation moves. The console's changes wait for the handoff when a tic is running beside the frame.
- A uniform's value is the level's if it set one, else its cvar's, else the declaration's.
  - A cvar is read each frame the shader runs, since mod cvars have no change callbacks.
  - A `server` cvar replicates to clients like any other.
- `gl_custompost 0` turns them all off. `listshaders` and `listuniforms` show what GLDEFS declared.

## What it costs

- **Nothing declared:** nothing; the frame is the frame without this feature.
- **Shaders declared:** from the first frame on, two frame-sized pictures to alternate between, plus the
  previous frame's picture if any shader reads it, so turning one on in play makes nothing (`scenarios
  madeinplay`).
- **Shaders running:** each is one full-screen pass.
  - The scene stages run over the share of the scene target that dynres draws, so they get cheaper when the
    scale drops.
  - Their last pass writes the frame at full size, so there is no separate upscale pass.
  - Three shaders (an invert, one reading the previous frame, and a screen one) measured 0.06 ms and
    0.03 ms of GPU and about 0.05 ms of CPU at 1280×720.

## How the backend makes them

- Each program is compiled at launch with the GLDEFS texture shaders (`PrecompileUserShaders`), and its
  pipeline is made on the device's pool.
- A pipeline lays out only the textures its compiled shader kept (`ComputeLayoutTextures`). One the
  SPIR-V optimizer left with no resources at all is drawn without a binding.
- A shader whose pipeline is not ready yet is skipped that frame. One that does not compile is left out
  with a note.
- Each (shader, picture) pair has one binding, made with the pictures and never pointed elsewhere.
- Beside GL nothing runs, since the swapchain cannot be read. In stereo, the scene stages run once over
  the blended picture.

Telemetry:
- `gpu/post_scene_ms`, `gpu/post_screen_ms` and `gpu/post/passes`
- the zones `draw/postprocess_scene`, `draw/postprocess_screen` and `load/post_shaders`
- `gpu/mem/tag/fua post *`

`fuactl scenarios postprocess` judges every stage, the orientation, the order, the weapon and the HUD,
`LastInputTexture`, `Texture` lines and a missing one reading white, a console uniform, and half scale. It
also checks a shader with no resources and one with an unused uniform and texture, and flips the optimizer
on and back off midway. `postprocessopt` runs the same checks launched with the optimizer on.

## Testing a shader change: both SPIR-V modes, always

Every shader change is tested with the SPIR-V optimizer **on and off** (`fua_dg_spirv_optimize 1` and `0`).
Each mode breaks in its own way, and a pass in one says nothing about the other:

- **Optimizer on** is what WebGPU always runs (the default, -1, turns it on there and off on Vulkan).
  - It deletes every declaration the code never reads, so a program can be left with fewer resources than
    its prolog declared, or with none.
  - A pipeline whose layout named resources the shader no longer had once hung Diligent in
    `CreateShaderResourceBinding`, the engine reaching 24 GB. The trigger was a shader that never reads
    `InputTexture`.
- **Optimizer off** keeps every declaration, which is exactly why such a bug never shows there.
- In both modes, include:
  - a shader that reads `InputTexture` and one that never does;
  - one declaring a uniform and a texture it never uses;
  - one with no resources at all (a constant colour);
  - flipping the cvar in the middle of a session, which lets every program go and makes them again.
- Where GL draws the same effect, compare with it too. Vulkan compilers do not keep float evaluation
  bit-identical between call sites, so hash noise such as `fract(sin(x) * k)` can draw differently
  than on GL (QPack's `Fire.fs` does, on UZDoom's Vulkan backend as well).
- Run any test engine under a watchdog that stops it past a few GB, so a runaway allocation fails the
  run instead of the machine.

## Files

- `postprocess.{h,cpp}`: the declarations, parsed from GLDEFS; the console commands; and what the renderer
  asks each frame (`PostShaderEnabledNow`, `PostShaderBlock`).
- `computation/postprocess_compute`: the stage and uniform types, the std140 layout, the prolog and the
  adapted source, the level's state table, the chains and ping-pong, the times and the regions.
- `features/hwrender/diligent/dgpostprocess.cpp`: the backend's programs, pictures and passes.

## In-engine hooks

- `features/hwshaders/hwshaders.cpp` `ParseHardwareShader`: a `postprocess` block goes to
  `ParsePostProcessShader`.
- `features/hwshaders/computation/hwshader_compute`: `ComputeIndentedLines`, shared with the texture
  shaders' skip note.
- `features/gldefs/gldefs.cpp` `gl_ParseDefs`: `ClearPostProcessShaders` before GLDEFS is read,
  `NotePostProcessRenderer` after.
- `g_level.h`/`g_level.cpp`: `FLevelLocals::fua_postShaders`, reset by `G_InitLevelLocals`.
- `features/gamecopy/level.h`/`gamecopy.cpp`: `LevelShaders`, taken by `TakeLevel` when its generation
  moved.
- `tools/boundary-rules.json`: `fua_postShaders` in the live level state a frame must not read.
- `features/draw2d/record.{h,cpp}`: `MarkViewQuads` and `ViewQuads`. `features/frame/playerview.cpp`
  marks them after the screen's tint.
- `features/hwrender/diligent/`:
  - `dgscene.cpp` `DrawSceneOnce`: the stages, the 2D from the first quad they left, and the present.
  - `dgtarget.cpp`: `BeginSceneFrame`'s `separate`, plus `SceneSeparate`, `SceneTexture` and
    `EnsureSceneTarget`.
  - `dg2d.cpp`: `Draw2D` takes a range of quads and a target.
  - `dgframe.cpp` `DrawOverlay`: the `screen` stage on a frame drawn afresh.
  - `dgskyportal.cpp`: `PictureVS`.
  - `dggputimer`: the two passes.
  - `dgpipelines.cpp`: `PrecompileUserShaders` starts them, and a change of `fua_dg_spirv_optimize` lets
    them go.
  - `dgwindow.cpp` `ShutdownBackend`: lets them go.
- `src/CMakeLists.txt`: `postprocess.cpp` in the engine's sources, `dgpostprocess.cpp` in
  `FUA_DILIGENT_SOURCES`.
