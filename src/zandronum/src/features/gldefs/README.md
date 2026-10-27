# features/gldefs

GLDEFS for every renderer, moved from GL's `gl/dynlights/gl_dynlight.cpp` and `gl/dynlights/gl_glow.cpp`.

- `gldefs.cpp`: the parser for the game's `*DEFS` lump and every `GLDEFS` (`gl_ParseDefs`), with each block
  handed to its owner: point, pulse, flicker and sector lights and their `object` frames (here),
  `glow` (`glow.cpp`), `skybox` (`features/materials/skyboxtexture.cpp`), `brightmap` and `detail`
  (`features/materials/textureinfo.cpp`), and `hardwareshader` (`features/hwshaders`). It also attaches the
  defined lights to actors as their frames change (`gl_SetActorLights`), and drops and remakes them
  (`gl_DeleteAllAttachedLights`, `gl_RecreateAllAttachedLights`) around a save or a light setting change.
- `lightsettings.cpp`: the light settings both renderers read (`r_lights`, `r_attachedlights`,
  `r_lights_checkside`, `r_lights_intensity`, `r_lights_size`, `r_light_sprites`, `r_light_particles`,
  `r_lights_additive`), moved from `gl/dynlights/gl_dynlight1.cpp` by way of `features/dynlights`;
  `r_lights` and `r_lights_additive` drop and remake the attached lights. `features/dynlights/dynlight.h`
  declares them.
- `glow.cpp`: `glow` blocks, which flats glow, and how much a glowing floor or ceiling lights a sprite
  (`gl_CheckSpriteGlow`).

`gl_SetActorLights` no longer clocks GL's `All` timer, which it left running when an actor had no state.

## In-engine hooks

- `features/frame/levelrenderer.cpp`: `Init` parses the definitions; `StateChanged`, `StartSerialize` and
  `EndSerialize` attach, drop and remake lights.
- `gl/renderer/gl_lightdata.h`: includes `gldefs.h` in place of the declarations it held;
  `gl/renderer/gl_renderer.cpp` drops the lights when GL shuts down.
- `gl/scene/*`: `glow.h` for the glowing flats.
- `r_nullrenderer.h` `Init`: `gl_GameDefsLump`, so a dedicated server reads the same lumps' post-process
  blocks; `gl_ParseDefs` hands the same name to `features/postprocess`.
