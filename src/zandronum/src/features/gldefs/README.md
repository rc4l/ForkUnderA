# features/gldefs

GLDEFS for every renderer, moved from GL's `gl/dynlights/gl_dynlight.cpp` and `gl/dynlights/gl_glow.cpp`.

- `gldefs.cpp`: the parser for the game's `*DEFS` lump and every `GLDEFS` (`gl_ParseDefs`), with each block
  handed to its owner: point, pulse, flicker and sector lights and their `object` frames (here),
  `glow` (`glow.cpp`), `skybox` (`features/materials/skyboxtexture.cpp`), `brightmap` and `detail`
  (`features/materials/textureinfo.cpp`), and `hardwareshader` (`features/hwshaders`). It also attaches the
  defined lights to actors as their frames change (`gl_SetActorLights`), and drops and remakes them
  (`gl_DeleteAllAttachedLights`, `gl_RecreateAllAttachedLights`) around a save or a light setting change.
- `computation/lighttags_compute`: which word in a light block is which tag and which blocks take it, so the
  five light blocks, `object` and `frame` share one parser (`gl_ParseLight`). It takes every tag UZDoom's
  parser takes. `intensity` multiplies the light's colour where it is drawn (`ADynamicLight::lightDefIntensity`,
  carried in the render copy's light record), as UZDoom's does. `dontlightactors` keeps the light off every
  sprite, model and particle (`features/dynlights/computation/shouldlight_compute`) while it still lights walls,
  flats and decals; `dontlightothers` lets only the actor carrying the light take it. `spot` is read and not yet
  acted on, and `noshadowmap`, `dontlightmap` and the top-level
  `colorization` read and dropped (we have no shadow maps, lightmaps or colourisation), so a UZDoom GLDEFS loads
  instead of stopping on "Unknown tag".
- `computation/lightsize_compute`: `size` and `secondarySize` clamped to UZDoom's 1 to 1024 (they were bytes, cut at
  255), and a sector light's `scale` as UZDoom stores it, which `ApplyProperties` hands the light as its `args[3]`
  (`features/dynlights/computation/sectorlight_compute` turns it into a radius).
- `computation/attenuate_compute`: UZDoom's attenuated lights. A light block's `attenuate 1` or `0` sets it, and one
  without the tag follows the level's MAPINFO `attenuatelights` (`features/mapinfo-attenuatelights`); map things
  9830 to 9834 (`PointLightAttenuated` and the rest, `+DYNAMICLIGHT.ATTENUATE` in `decorate.z`) always are. The
  flag reaches the scene shader in the light's kind (`ComputeDynLightKind`), where a wall or flat takes an attenuated
  light by the cosine of the angle it arrives at, as UZDoom's `material_normal.fp` does; sprites and particles take
  it as before, as UZDoom's do. Models, which UZDoom always lights this way per pixel, still take every light on
  the CPU as sprites do. The top-level `lightsizefactor` scales `size` and `secondarySize` of the definitions after
  it in its lump and what that lump includes, except those with `attenuate 0`: UZDoom reads its unset tag (-1)
  through an unsigned getter, so lights without the tag are scaled too.
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
